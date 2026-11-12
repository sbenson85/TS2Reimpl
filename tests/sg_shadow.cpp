// Differential tests of nTSSG::cShadow (lane 261109e, scenegraph LG7d; src/sg/cShadow.h).
//
//   sg_shadow_life  one shadow's life on each side: built by the factory 0x355700 (the bytes it leaves unwritten set
//       alike) or by the constructor 0x354320 on a block filled alike, often brought up first (Init, an object, enabled,
//       shown), then random calls through its table: Init / Shutdown, SetObject (two objects), SetMode (0-5), SetEnabled
//       (bytes and words whose low byte differs), Suspend / Resume, Show / Hide, Update (most often), Refresh, slots 14-17,
//       the getters 18-21, HasMaterial, QueryInterface (null outs too), AddRef / Release, and world changes between calls
//       (the manager's change count and option bits, the owner's id, the camera). After each call the object is noted
//       word by word (vtable and string buffers left out; each material's text, size and capacity). At the end it is
//       deleted (the last Release, or slot 23 with 1), then the fakes' counts and the heap.
//
//   sg_shadow_footprint  ClipPlanesMeet 0x3ad380 and ShadowFootprint 0x3aced0 on projections (orthographic, rotated,
//       degenerate, mirrored, perspective) and on matrices drawn from a value set with zeros, tiny values and NaN, with
//       NaN pairs of different payloads planted (operand order); the outputs prefilled, so the untouched ones show.
//
//   sg_shadow_helpers  ObjectBounds 0x3558d0 (masks), FindSubsetMaterial 0x3acd30, SubsetMaterial 0x355470 and the layer
//       swap 0x354630 (masks swapping at each level) called directly on the objects' trees; the shadow material 0x3546b0
//       (names, debug bytes with junk upper bits, the manager's indices); the object filter (0x14 bytes) built alike on
//       each side (the original's by its words, its tables the image's; ours constructed), Accept 0x356a70 on the trees,
//       then destroyed through the +4 table's slot 3 (the original's thunk 0x3545a3 or its body 0x354580 directly) with
//       its table words junk, flags 0 and 1 (the delete recorded).
//
// The world: two objects (a class with a virtual base: the face table, the vbtable at +4, the cIGZUnknown base) whose
// face is a tree node (children in value[], boxes, flags, world transforms, visibility with junk upper bits, layer
// states, slot 120's owner: its 13 / 43 / 47 / 98 / 144 / 189), shape holders reached by QueryInterface (counted;
// sometimes written but refused) and their shapes' material names (null, "GUOBOff" either case, "", "VisGUOBMaterial", a
// heap-sized name), the shadow manager (its change record with sizes, blur radii and resolutions, shadows acquired and
// released, textures, matrices with stamps, scales, boxes), the camera's owner (camera, manager, grids handing out the
// projectors, levels), the director's creates by class (the viewer with two children, our node, the lift node, the
// shape holder that becomes the drawable, the quad's shape, the material definition), the render group registry (bits
// past 31 too), the material manager (cell 0x10f8fcc: indices, -1 too), the scene's root, placement, the cached
// singleton B (cell 0x10cc28c). The names CreateResources makes carry the shadow's address: their logged text has it as
// "<shadow>" (Unaddress). The viewer holds the filter it is handed (the last one released at the end, deleting it) and
// tries it on the trees.
// Stood in on both sides (code patches over the image's functions; ours reach them through OriginalFunction, or the
// patch covers ours too), noted: the getters 0x20defb (the camera's owner), 0x20e0b2 (placement) and 0x211ce1 (the
// scene), the director 0x1ae18d, the registry 0xaeb370, the subset binder 0x3ad4a0, the relative transform 0xaeb2c0
// (unchanged, ULP-nudged across the 32-step match, negated or NaN, its out or another), the animated face 0xae1ed0 and
// named node 0xae7490 (FindRootBone's), the terrain query 0x7e2cc0, the level of ground 0x7e28d0, the quadrant test
// 0x7e0920 (junk upper bits), the render target's constructor 0x3c9ed0 (the target fake embedded in the block, its index
// -1; its slot 6 sets it, -1 now and then), the quad geometry 0xaf16f0 (ours too: MakeQuadMesh), and the game's operator new / delete / free (to
// ours, counted and normalised). CreateResources (row LG7d3), PlaceProjectors, UpdateSettings and FloorTest (row LG7d2)
// run on both sides; QuaternionAngles 0x1a77c0 is the image's, and the shape material handle 0xae8820 / 0xae8870 each
// side's own (ours since lane 261111d), both on the manager cell.
#include "sgfx.h"

#include "code_patch.h"

#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/cShadow.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <utility>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}
template <size_t N> constexpr std::array<Slot, N> Table()
{
    const At none[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
    return Table<N>(none);
}

// A tree node (the objects' faces, their children, the shape holders, our node, the scene's root): the composition
// tree's slots, 29 (the drawable's flag), cITransformNode's 64 / 65, cIBoundedNode's 70 / 72, cIRenderableNode's 77 / 78
// / 80 / 86, cIShapeRefNode's 93 / 94 / 97, 112 (visible), 120 (the owner), 124.
constexpr At kNodeAt[] = {{29, {2, Ret::Int, true, "SetFlag", "ub"}},
                          {34, {0, Ret::Int, true, "GetParent", ""}},
                          {38, {0, Ret::Int, true, "GetNextSibling", ""}},
                          {42, {1, Ret::Int, true, "GetChildren", "a"}},
                          {45, {1, Ret::Int, true, "AddChild", "a"}},
                          {46, {1, Ret::Int, true, "RemoveChild", "a"}},
                          {48, {1, Ret::Int, true, "TestFlag", "u"}},
                          {54, {1, Ret::Int, true, "GetWorldTransform", "a"}},
                          {64, {1, Ret::Int, true, "SetTranslation", "F3"}},
                          {65, {1, Ret::Int, true, "SetRotation", "F4"}},
                          {70, {0, Ret::Int, true, "IsEverywhere", ""}},
                          {72, {1, Ret::Int, true, "GetBoundBox", "a"}},
                          {77, {1, Ret::Int, true, "AddLayer", "s"}},
                          {78, {1, Ret::Int, true, "AddLayerBits", "u"}},
                          {80, {1, Ret::Int, true, "RemoveLayerBits", "u"}},
                          {86, {1, Ret::Int, true, "GetLayerState", "a"}},
                          {92, {1, Ret::Int, true, "GetShapesConst", "a"}},
                          {93, {1, Ret::Int, true, "GetShapes", "a"}},
                          {94, {1, Ret::Int, true, "AddShape", "a"}},
                          {97, {0, Ret::Int, true, "AnyShapeSlot52", ""}},
                          {112, {0, Ret::Int, true, "Visible", ""}},
                          {120, {0, Ret::Int, true, "Owner", ""}},
                          {124, {0, Ret::Int, true, "Slot124", ""}}};
constexpr auto kNodeSlots = Table<125>(kNodeAt);
constexpr drawfx::Iface kNode{"Node", 125, kNodeSlots.data(), false, drawfx::kTestTable};

// A shape: 34 a material bound to a layer (the quad's), 41 a part added (the quad's geometry), 47 the part's object (a
// mesh) at a level, 53 the material it names for a subset, 57 the shown level.
constexpr At kShapeAt[] = {{34, {2, Ret::Int, true, "BindMaterial", "ss"}},
                           {41, {2, Ret::Int, true, "AddPart", "au"}},
                           {47, {1, Ret::Int, true, "PartObjectAtLod", "u"}},
                           {53, {1, Ret::Int, true, "MaterialNameOf", "s"}},
                           {57, {0, Ret::Int, true, "GetShownLod", ""}}};
constexpr auto kShapeSlots = Table<58>(kShapeAt);
constexpr drawfx::Iface kShape{"Shape", 58, kShapeSlots.data(), false, drawfx::kTestTable};

// A part's mesh: 29 its geometry, 44 a subset's elements as a counted cursor (hidden out {walker, mark, end}).
constexpr At kMeshAt[] = {{29, {0, Ret::Int, true, "Geometry", ""}}, {44, {2, Ret::Int, true, "SubsetCursor", "as"}}};
constexpr auto kMeshSlots = Table<45>(kMeshAt);
constexpr drawfx::Iface kMesh{"Mesh", 45, kMeshSlots.data(), false, drawfx::kTestTable};
// The cursor's walker: 4 at the end (the end word), 5 the element's index, 6 the next.
constexpr At kWalkerAt[] = {{4, {1, Ret::Int, true, "AtEnd", "a"}},
                            {5, {0, Ret::Int, true, "Index", ""}},
                            {6, {0, Ret::Int, true, "Step", ""}}};
constexpr auto kWalkerSlots = Table<7>(kWalkerAt);
constexpr drawfx::Iface kWalker{"Walker", 7, kWalkerSlots.data(), false, drawfx::kTestTable};
// The geometry: 5 an element by index.
constexpr At kGeometryAt[] = {{5, {1, Ret::Int, true, "Element", "u"}}};
constexpr auto kGeometrySlots = Table<6>(kGeometryAt);
constexpr drawfx::Iface kGeometry{"Geometry", 6, kGeometrySlots.data(), false, drawfx::kTestTable};
// The level service 0x20df51: 22 whether a height's level is shown.
constexpr At kLevelShownAt[] = {{22, {1, Ret::Int, true, "Shown", "f"}}};
constexpr auto kLevelShownSlots = Table<23>(kLevelShownAt);
constexpr drawfx::Iface kLevelShown{"LevelShown", 23, kLevelShownSlots.data(), false, drawfx::kTestTable};
// Placement's walls (its slot 39): 44 a tile edge's test.
constexpr At kWallsAt[] = {{44, {1, Ret::Int, true, "EdgeTest", "F6"}}};
constexpr auto kWallsSlots = Table<45>(kWallsAt);
constexpr drawfx::Iface kWalls{"Walls", 45, kWallsSlots.data(), false, drawfx::kTestTable};

// The object's owner (its slot 120): 13 a flag, 43 the owner id, 47 the id the filter keeps (its low half), 98 its
// kind, 144 / 189 two tests.
constexpr At kOwnerAt[] = {{13, {1, Ret::Int, true, "Flag", "u"}},
                           {42, {1, Ret::Int, true, "Position", "a"}},
                           {43, {0, Ret::Int, true, "OwnerId", ""}},
                           {47, {0, Ret::Int, true, "FilterId", ""}},
                           {98, {0, Ret::Int, true, "Kind", ""}},
                           {144, {0, Ret::Int, true, "Test144", ""}},
                           {189, {0, Ret::Int, true, "Test189", ""}}};
constexpr auto kOwnerSlots = Table<190>(kOwnerAt);
constexpr drawfx::Iface kOwner{"Owner", 190, kOwnerSlots.data(), false, drawfx::kTestTable};

// cIShadowManager (src/sg/cShadowManager.h).
constexpr At kManagerAt[] = {{8, {2, Ret::Int, true, "AcquireShadow", "au"}},
                             {9, {1, Ret::Int, true, "ReleaseShadow", "u"}},
                             {12, {0, Ret::Int, true, "ChangeCount", ""}},
                             {13, {1, Ret::Int, true, "ShadowTexture", "u"}},
                             {14, {3, Ret::Int, true, "GetShadowMatrix", "uaa"}},
                             {15, {3, Ret::Int, true, "GetShadowScale", "uaa"}},
                             {16, {2, Ret::Int, true, "GetShadowBox", "au"}}};
constexpr auto kManagerSlots = Table<17>(kManagerAt);
constexpr drawfx::Iface kManager{"ShadowManager", 17, kManagerSlots.data(), false, drawfx::kTestTable};

// The camera's owner: 38 the levels, 45 a grid, 49 the camera, 50 the shadow manager.
constexpr At kCameraOwnerAt[] = {{38, {0, Ret::Int, true, "Levels", ""}},
                                 {45, {1, Ret::Int, true, "Grid", "u"}},
                                 {49, {0, Ret::Int, true, "Camera", ""}},
                                 {50, {0, Ret::Int, true, "ShadowManager", ""}}};
constexpr auto kCameraOwnerSlots = Table<51>(kCameraOwnerAt);
constexpr drawfx::Iface kCameraOwner{"CameraOwner", 51, kCameraOwnerSlots.data(), false, drawfx::kTestTable};

constexpr At kLevelsAt[] = {{19, {1, Ret::Int, true, "Level", "u"}}};
constexpr auto kLevelsSlots = Table<20>(kLevelsAt);
constexpr drawfx::Iface kLevels{"Levels", 20, kLevelsSlots.data(), false, drawfx::kTestTable};
constexpr At kLevelAt[] = {{25, {0, Ret::Int, true, "Grid", ""}}};
constexpr auto kLevelSlots = Table<26>(kLevelAt);
constexpr drawfx::Iface kLevel{"Level", 26, kLevelSlots.data(), false, drawfx::kTestTable};

// A grid: 6 a projector in, 8 a projector made on it (by name, an origin and a matrix), 9 a projector out.
constexpr At kGridAt[] = {{6, {1, Ret::Int, true, "Insert", "a"}},
                          {8, {3, Ret::Int, true, "MakeProjector", "sF4F16"}},
                          {9, {1, Ret::Int, true, "Remove", "a"}}};
constexpr auto kGridSlots = Table<10>(kGridAt);
constexpr drawfx::Iface kGrid{"Grid", 10, kGridSlots.data(), false, drawfx::kTestTable};

// The camera: 30 its position, 31 its view (each through a buffer).
constexpr At kCameraAt[] = {{30, {1, Ret::Int, true, "Position", "a"}}, {31, {1, Ret::Int, true, "View", "a"}}};
constexpr auto kCameraSlots = Table<32>(kCameraAt);
constexpr drawfx::Iface kCamera{"Camera", 32, kCameraSlots.data(), false, drawfx::kTestTable};

// A projector: 3 on, 5 the view (or a colour: its floats noted by the handler), 6 its kind, 7 the texture, 8 the
// quadrant test, 9 the rectangle, projection and scale, 10 the rectangle and a view-projection.
constexpr At kProjectorAt[] = {{3, {1, Ret::Int, true, "SetOn", "b"}},
                               {5, {1, Ret::Int, true, "SetView", "a"}},
                               {6, {1, Ret::Int, true, "SetKind", "u"}},
                               {7, {1, Ret::Int, true, "SetTexture", "u"}},
                               {8, {1, Ret::Int, true, "SetQuadrant", "u"}},
                               {9, {5, Ret::Int, true, "Place", "RF16F2F2u"}},
                               {10, {3, Ret::Int, true, "Frame", "RF16u"}}};
constexpr auto kProjectorSlots = Table<11>(kProjectorAt);
constexpr drawfx::Iface kProjector{"Projector", 11, kProjectorSlots.data(), false, drawfx::kTestTable};

// nTSSG::cBlurredRenderTarget (embedded in the block CreateResources allocates): 6 its texture (a name, the width and
// height), 7 detach, 8 the blur radius.
constexpr At kTargetAt[] = {{6, {3, Ret::Int, true, "MakeTexture", "suu"}},
                            {7, {0, Ret::Int, true, "Detach", ""}},
                            {8, {1, Ret::Int, true, "SetBlurRadius", "u"}}};
constexpr auto kTargetSlots = Table<9>(kTargetAt);
constexpr drawfx::Iface kTarget{"Target", 9, kTargetSlots.data(), false, drawfx::kTestTable};

// The viewer (nGZSceneGraph::cIViewerRefNode): the tree's 34 / 38 / 42 / 48, 54 its world transform (the reference
// FitShadowCamera's bounds are taken in), 65 its rotation, 66 the bound flag, cIRenderableNode's 78 / 80, CreateResources'
// 94 / 97 / 100 (the render target) / 102 (the drawable) / 103 (the filter) / 124 (the entries), 120 the owner (the
// filter's walk up the tree reaches it), 131 the pixel aspect, 136
// the orthographic box, 139 an entry (its projection at +0x74), 140 a view, 143 a point's clip coordinates.
constexpr At kViewerAt[] = {{34, {0, Ret::Int, true, "GetParent", ""}},
                            {38, {0, Ret::Int, true, "GetNextSibling", ""}},
                            {42, {1, Ret::Int, true, "GetChildren", "a"}},
                            {48, {1, Ret::Int, true, "TestFlag", "u"}},
                            {54, {1, Ret::Int, true, "GetWorldTransform", "a"}},
                            {65, {1, Ret::Int, true, "SetRotation", "F4"}},
                            {66, {1, Ret::Int, true, "SetBoundFlag", "b"}},
                            {78, {1, Ret::Int, true, "AddLayerBits", "u"}},
                            {80, {1, Ret::Int, true, "RemoveLayerBits", "u"}},
                            {94, {1, Ret::Int, true, "Slot94", "u"}},
                            {97, {1, Ret::Int, true, "Slot97", "u"}},
                            {100, {1, Ret::Int, true, "SetTarget", "a"}},
                            {102, {1, Ret::Int, true, "SetDrawable", "a"}},
                            {103, {1, Ret::Int, true, "SetFilter", "a"}},
                            {120, {0, Ret::Int, true, "Owner", ""}},
                            {124, {1, Ret::Int, true, "ResizeEntries", "u"}},
                            {131, {2, Ret::Int, true, "SetPixelAspect", "uf"}},
                            {136, {7, Ret::Int, true, "SetOrtho", "uffffff"}},
                            {139, {1, Ret::Int, true, "GetEntry", "u"}},
                            {140, {2, Ret::Int, true, "GetView", "au"}},
                            {143, {3, Ret::Int, true, "WorldToClip", "aF3u"}}};
constexpr auto kViewerSlots = Table<144>(kViewerAt);
constexpr drawfx::Iface kViewer{"Viewer", 144, kViewerSlots.data(), false, drawfx::kTestTable};

// A root bone: 54 its world transform.
constexpr At kBoneAt[] = {{54, {1, Ret::Int, true, "GetWorldTransform", "a"}}};
constexpr auto kBoneSlots = Table<55>(kBoneAt);
constexpr drawfx::Iface kBone{"Bone", 55, kBoneSlots.data(), false, drawfx::kTestTable};

constexpr At kSceneAt[] = {{17, {0, Ret::Int, true, "Root", ""}}};
constexpr auto kSceneSlots = Table<18>(kSceneAt);
constexpr drawfx::Iface kScene{"Scene", 18, kSceneSlots.data(), false, drawfx::kTestTable};

constexpr At kPlacementAt[] = {{39, {0, Ret::Int, true, "Walls", ""}}, {72, {2, Ret::Int, true, "InsideLot", "ff"}}};
constexpr auto kPlacementSlots = Table<73>(kPlacementAt);
constexpr drawfx::Iface kPlacement{"Placement", 73, kPlacementSlots.data(), false, drawfx::kTestTable};

constexpr At kSingletonAt[] = {{28, {0, Ret::Int, true, "Slot28", ""}}};
constexpr auto kSingletonSlots = Table<29>(kSingletonAt);
constexpr drawfx::Iface kSingleton{"SingletonB", 29, kSingletonSlots.data(), false, drawfx::kTestTable};

constexpr At kAnimFaceAt[] = {{52, {1, Ret::Int, true, "Bone", "u"}}};
constexpr auto kAnimFaceSlots = Table<53>(kAnimFaceAt);
constexpr drawfx::Iface kAnimFace{"AnimatedFace", 53, kAnimFaceSlots.data(), false, drawfx::kTestTable};

constexpr auto kUnknownSlots = Table<3>();
constexpr drawfx::Iface kUnknownFace{"Unknown", 3, kUnknownSlots.data(), false, drawfx::kTestTable};

// cIGZCOM: 3 GetClassObject (class, interface, out).
constexpr At kDirectorAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kDirectorSlots = Table<4>(kDirectorAt);
constexpr drawfx::Iface kDirector{"COM", 4, kDirectorSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cIMaterialDefinition: 10 its name, 12 its type (the shader), 13 a property (name, value).
constexpr At kMaterialDefAt[] = {{10, {1, Ret::Int, true, "SetMaterialName", "s"}},
                                 {12, {1, Ret::Int, true, "SetMaterialType", "s"}},
                                 {13, {2, Ret::Int, true, "SetProperty", "ss"}}};
constexpr auto kMaterialDefSlots = Table<14>(kMaterialDefAt);
constexpr drawfx::Iface kMaterialDef{"MaterialDefinition", 14, kMaterialDefSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cIMaterialManager (cell 0x10f8fcc): 20 / 21 an index's reference taken / dropped, 23 a material added
// (with flags; its index).
constexpr At kMaterialsAt[] = {{20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                               {21, {1, Ret::Int, true, "ReleaseIndex", "u"}},
                               {23, {2, Ret::Int, true, "AddMaterial", "au"}}};
constexpr auto kMaterialsSlots = Table<24>(kMaterialsAt);
constexpr drawfx::Iface kMaterials{"MaterialManager", 24, kMaterialsSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cIRenderGroupRegistry: 3 a group's bit by name.
constexpr At kGroupsAt[] = {{3, {1, Ret::Int, true, "BitOfName", "s"}}};
constexpr auto kGroupsSlots = Table<4>(kGroupsAt);
constexpr drawfx::Iface kGroups{"RenderGroups", 4, kGroupsSlots.data(), false, drawfx::kTestTable};

template <unsigned kSlot, class R, class... Args> R Virtual(void *self, Args... args)
{
    using F = R(__thiscall *)(void *, Args...);
    return (*reinterpret_cast<F *const *>(self))[kSlot](self, args...);
}

constexpr uint32_t kSingletonCell = 0x10cc28c;
constexpr uint32_t kMaterialsCell = 0x10f8fcc; // the material manager (GetCachedQueriedObject's)
constexpr uint32_t kShapeRefIid = 0xa516cd10;
constexpr uint32_t kSize = sizeof(cShadow);
constexpr unsigned kWords = kSize / 4;
constexpr uint32_t kObjectTag = 0x7c000000, kBufferTag = 0x7b000000;
constexpr unsigned kObjects = 2, kKids = 4, kHolders = 2, kShapes = 4, kGrids = 3, kBones = 3;

const float kNan = std::bit_cast<float>(0x7fc00000u);

// An object: a class with a virtual base, its vbtable at +4 ([1] the cIGZUnknown base's offset from it).
struct Holder {
    void *faceTable;
    const int32_t *vbptr; // +4
    uint32_t pad[2];
    Fake unknown;
};
const int32_t kHolderBases[2] = {0, int32_t(offsetof(Holder, unknown)) - 4};

// What the fakes hand out by pointer (one block, registered).
struct Buffers {
    int32_t record[12]; // the manager's change record: [0] the change count, [11] the option bits
    float view[16];     // the camera's view
    float relative[7];  // 0xaeb2c0's other answer
    float position[3];  // the owner's other position answer
    uint8_t elements[4][0x38]; // the geometry's elements (a box at +0x1c)
    float entry[0x74 / 4 + 16]; // the viewer's entry (its projection at +0x74)
    float otherView[7];         // the viewer's view answered somewhere other than the out
    uint32_t layers[2];         // a node's layer state answered somewhere other than the out
};

struct World {
    Fake objectFaces[kObjects], kids[kKids], holders[kHolders], shapes[kShapes], owner, manager, cameraOwner, levels, level,
        grids[kGrids], camera, projectors[2], drawable, viewer, target, node, root, scene, placement, singleton, animFace,
        bones[kBones], meshes[2], walker, mark, cursorEnd, geometry, levelShown, walls, director, lift, quad, quadGeometry,
        material, materials, groups, viewerKids[2];
    Holder objects[kObjects];
    Fake *shapeList[kShapes];
    Buffers buf;
    float cam[3];
    float lastRelative[7];
    unsigned boneBase;
    unsigned walkerAt, walkerCount;
    unsigned transforms, projectorsMade; // the director's transform nodes and the grids' projectors handed out
    void *shadow;                        // the shadow under test (its address is in the names it makes)
    uint8_t *filter;                     // the filter the viewer holds
    int32_t ownerId;
    bool ownerNull;
    uint32_t lcg;
    uint32_t Next() { return lcg = lcg * 1664525u + 1013904223u, lcg >> 8; }
};
World gw;

uint8_t *Bytes(void *o) { return static_cast<uint8_t *>(o); }
uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }
template <class T> T *Arg(uint32_t a) { return reinterpret_cast<T *>(uintptr_t(a)); }
// A junk upper part over a low byte (what a callee returning a byte may leave in eax).
uint32_t Junk(uint32_t low) { return (gw.Next() << 8) | low; }

// ---- the tree ----

// A tree node's links in value[]: its parent, its first child, its next sibling.
constexpr unsigned kParentWord = 0, kChildWord = 1, kSiblingWord = 2;
Fake *Link(uint32_t word) { return Arg<Fake>(word); }

void Unlink(Fake &child)
{
    Fake *const parent = Link(child.value[kParentWord]);
    if (!parent) return;
    uint32_t *link = &parent->value[kChildWord];
    while (*link && *link != drawfx::P(&child)) link = &Link(*link)->value[kSiblingWord];
    if (*link) *link = child.value[kSiblingWord];
    child.value[kParentWord] = child.value[kSiblingWord] = 0;
    --child.refs;
}
void Attach(Fake &parent, Fake &child)
{
    Unlink(child);
    child.value[kSiblingWord] = parent.value[kChildWord];
    child.value[kParentWord] = drawfx::P(&parent);
    parent.value[kChildWord] = drawfx::P(&child);
    ++child.refs;
}

uint32_t TreeParent(Fake &self, const uint32_t *) { return self.value[kParentWord]; }
uint32_t TreeSibling(Fake &self, const uint32_t *) { return self.value[kSiblingWord]; }
uint32_t TreeChildren(Fake &self, const uint32_t *a)
{
    uint32_t *const range = Arg<uint32_t>(a[0]);
    range[0] = range[1] = self.value[kChildWord];
    range[2] = 0;
    return a[0];
}
uint32_t TreeAdd(Fake &self, const uint32_t *a)
{
    Attach(self, *Link(a[0]));
    return 0;
}
uint32_t TreeRemove(Fake &self, const uint32_t *a)
{
    Fake &child = *Link(a[0]);
    if (child.value[kParentWord] == drawfx::P(&self)) Unlink(child);
    return 0;
}
// Flag 8 (the filter's refusal) set a third of the time, the others three quarters.
uint32_t TreeFlag(Fake &, const uint32_t *a) { return a[0] == 8 ? gw.Next() % 3 == 0 : gw.Next() % 4 != 0; }
// GetLayerState (hidden out, two words; the filter reads the first): no bits, one, all or junk; in the out or elsewhere.
uint32_t TreeLayers(Fake &, const uint32_t *a)
{
    uint32_t *const out = gw.Next() % 5 == 0 ? gw.buf.layers : Arg<uint32_t>(a[0]);
    const uint32_t r = gw.Next() % 4;
    out[0] = r == 0 ? 0 : r == 1 ? 1u << gw.Next() % 32 : r == 2 ? 0xffffffffu : gw.Next() << 8 | gw.Next() % 256;
    out[1] = gw.Next();
    return drawfx::P(out);
}
uint32_t TreeEverywhere(Fake &, const uint32_t *) { return gw.Next() % 4 == 0; }
uint32_t TreeAnyShape(Fake &, const uint32_t *) { return gw.Next() % 5 != 0; }
uint32_t TreeVisible(Fake &, const uint32_t *) { return Junk(gw.Next() % 5 != 0); }
uint32_t TreeOwner(Fake &, const uint32_t *) { return gw.ownerNull ? 0 : drawfx::P(&gw.owner); }
uint32_t TreeName(Fake &, const uint32_t *) { return gw.Next(); }

// World transforms (translation, then the quaternion x, y, z, w).
const float kTransforms[4][7] = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                                 {1.0f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f},
                                 {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.70710677f, 0.70710677f},
                                 {0.5f, -1.0f, 2.0f, 0.18257418f, 0.36514837f, 0.54772256f, 0.73029674f}};
uint32_t TreeWorld(Fake &, const uint32_t *a)
{
    std::memcpy(Arg<float>(a[0]), kTransforms[gw.Next() % 4], sizeof(kTransforms[0]));
    return a[0];
}
// The viewer's clip coordinates for a point: each inside, on an edge, just past one, or NaN.
uint32_t ViewerClip(Fake &, const uint32_t *a)
{
    static const float kClips[] = {0.0f,  0.5f,       -0.5f, 1.0f,       -1.0f, 1.0000001f, -1.0000001f,
                                   0.25f, -0.000001f, 2.0f,  0.9999999f, 0.0f,  0.5f,       kNan};
    float *const out = Arg<float>(a[0]);
    const bool inside = gw.Next() % 2 == 0;
    for (unsigned i = 0; i < 3; ++i)
        out[i] = inside ? kClips[gw.Next() % 4] : kClips[gw.Next() % std::size(kClips)];
    return a[0];
}
// GetBoundBox (hidden out, six floats; always written): sometimes flat, NaN or inside out.
uint32_t TreeBox(Fake &, const uint32_t *a)
{
    float *const b = Arg<float>(a[0]);
    const uint32_t r = gw.Next() % 8;
    static const float kSizes[] = {0.1f, 0.5f, 1.0f, 1.5f, 2.0f, 3.5f};
    const float s = kSizes[gw.Next() % std::size(kSizes)];
    for (unsigned i = 0; i < 3; ++i) {
        const float base = float(int32_t(gw.Next() % 7) - 3) * 0.5f;
        b[i] = base;
        b[i + 3] = r == 0 ? base : base + s * float(i + 1) * 0.5f;
    }
    if (r == 1 || r == 7) { // a quarter of the boxes: PlaceProjectors meets them with mode 1's NaN extents
        if (gw.Next() % 2) {
            b[gw.Next() % 6] = kNan;
        } else { // a NaN pair on one axis with different payloads, so that operand order shows
            const unsigned k = gw.Next() % 3;
            b[k] = std::bit_cast<float>(0xffc00000u | (1u + gw.Next() % 0x3ffffeu));
            b[k + 3] = std::bit_cast<float>(0x7fc00000u | (1u + gw.Next() % 0x3ffffeu));
        }
    } else if (r == 2) {
        const unsigned k = gw.Next() % 3;
        const float t = b[k];
        b[k] = b[k + 3], b[k + 3] = t;
    }
    return a[0];
}
// GetShapes (hidden out cShapeRange: first, current, end): a run of the shape list.
uint32_t TreeShapes(Fake &, const uint32_t *a)
{
    const uint32_t first = gw.Next() % kShapes, count = gw.Next() % (kShapes - first + 1);
    uint32_t *const range = Arg<uint32_t>(a[0]);
    range[0] = drawfx::P(&gw.shapeList[first]);
    range[1] = range[2] = drawfx::P(&gw.shapeList[first + count]);
    return a[0];
}
// QueryInterface for the shape holder: one of the holders (counted), sometimes written but refused, sometimes none.
uint32_t TreeQuery(Fake &, const uint32_t *a)
{
    if (a[0] != kShapeRefIid) return 0;
    const uint32_t r = gw.Next() % 6;
    if (r == 0) return 0;
    Fake *const holder = &gw.holders[gw.Next() % kHolders];
    ++holder->refs;
    *Arg<Fake *>(a[1]) = holder;
    return r != 1;
}

void MakeNode(Fake &f, uint32_t id)
{
    f.Reset<kNode>(id);
    f.On(0, &TreeQuery);
    f.On(34, &TreeParent);
    f.On(38, &TreeSibling);
    f.On(42, &TreeChildren);
    f.On(45, &TreeAdd);
    f.On(46, &TreeRemove);
    f.On(48, &TreeFlag);
    f.On(54, &TreeWorld);
    f.On(70, &TreeEverywhere);
    f.On(72, &TreeBox);
    f.On(86, &TreeLayers);
    f.On(92, &TreeShapes);
    f.On(93, &TreeShapes);
    f.On(97, &TreeAnyShape);
    f.On(112, &TreeVisible);
    f.On(120, &TreeOwner);
    f.On(124, &TreeName);
}

const char kLongMaterial[] = "a_long_shadow_material_name_x";
uint32_t ShapeMaterial(Fake &, const uint32_t *)
{
    static const char *const kNames[] = {nullptr,       nullptr, nullptr, "GUOBOff",         "guoboff",
                                         "shadowmat_a", "",      nullptr, "VisGUOBMaterial", kLongMaterial, "s"};
    return drawfx::P(kNames[gw.Next() % std::size(kNames)]);
}

// ---- the owner, the manager, the camera ----

uint32_t OwnerFlag(Fake &, const uint32_t *) { return Junk(gw.Next() % 4 == 0); }
uint32_t OwnerId(Fake &, const uint32_t *) { return uint32_t(gw.ownerId); }
uint32_t OwnerKind(Fake &, const uint32_t *)
{
    static const int32_t kKinds[] = {0, 7, 8, 10, 12, 13, -1};
    return uint32_t(kKinds[gw.Next() % std::size(kKinds)]);
}
uint32_t OwnerTest(Fake &, const uint32_t *)
{
    static const uint32_t kAnswers[] = {1, 1, 0, 0x100, 0x101};
    return kAnswers[gw.Next() % std::size(kAnswers)];
}

// The id the filter keeps (a word whose low half it compares): 0-2 under junk.
uint32_t OwnerFilterId(Fake &, const uint32_t *)
{
    const uint32_t high = gw.Next() << 16;
    return high | gw.Next() % 3;
}

uint32_t ManagerAcquire(Fake &, const uint32_t *)
{
    static const int32_t kIndices[] = {-1, 0, 2, 5, 2};
    return uint32_t(kIndices[gw.Next() % std::size(kIndices)]);
}
uint32_t ManagerChange(Fake &, const uint32_t *) { return drawfx::P(gw.buf.record); }
uint32_t ManagerTexture(Fake &, const uint32_t *a) { return gw.Next() % 3 == 0 ? 0 : 0x300 + a[0]; }

// The shadow's projections (row-major as the footprint reads them).
const float kProjections[6][16] = {
    {0.5f, 0.0f, 0.0f, 0.1f, 0.0f, 0.25f, 0.0f, 0.3f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    {0.3f, 0.4f, 0.0f, 0.0f, -0.4f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 0.2f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {-0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -0.1f, 0.0f},
    {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.2f, -0.1f, 0.0f, 1.0f}};

uint32_t ManagerMatrix(Fake &, const uint32_t *a)
{
    uint32_t *const stamp = Arg<uint32_t>(a[2]);
    drawfx::Note("stamp", *stamp);
    if (gw.Next() % 5 == 0) return 0;
    float *const out = Arg<float>(a[1]);
    std::memcpy(out, kProjections[gw.Next() % 6], sizeof(kProjections[0]));
    if (gw.Next() % 4 == 0) out[gw.Next() % 16] += 0.125f;
    *stamp = 0x100 + gw.Next() % 8;
    return 1;
}
// The viewer's entry: one of the projections at +0x74 (the words before it junk), sometimes nudged.
uint32_t ViewerEntry(Fake &, const uint32_t *)
{
    float *const entry = gw.buf.entry;
    for (unsigned i = 0; i < 0x74 / 4; ++i) entry[i] = float(gw.Next() % 64) - 32.0f;
    std::memcpy(entry + 0x74 / 4, kProjections[gw.Next() % 6], sizeof(kProjections[0]));
    if (gw.Next() % 4 == 0) entry[0x74 / 4 + gw.Next() % 16] += 0.25f;
    return drawfx::P(entry);
}
// The viewer's view (hidden out, a rigid transform): one of the transforms, in the out or elsewhere.
uint32_t ViewerView(Fake &, const uint32_t *a)
{
    float *const out = gw.Next() % 4 == 0 ? gw.buf.otherView : Arg<float>(a[0]);
    std::memcpy(out, kTransforms[gw.Next() % 4], sizeof(kTransforms[0]));
    if (gw.Next() % 3 == 0) out[gw.Next() % 3] += 1.5f;
    return drawfx::P(out);
}
uint32_t ManagerScale(Fake &, const uint32_t *a)
{
    if (gw.Next() % 3 == 0) return 0;
    float *const offset = Arg<float>(a[1]);
    float *const scale = Arg<float>(a[2]);
    offset[0] = float(a[0]) * 0.25f, offset[1] = 0.5f;
    scale[0] = 0.5f + float(gw.Next() % 4), scale[1] = 2.0f;
    return 0;
}
uint32_t ManagerBox(Fake &, const uint32_t *a)
{
    float *const box = Arg<float>(a[0]);
    for (unsigned i = 0; i < 7; ++i) box[i] = float(int32_t(gw.Next() % 9) - 4) * 0.25f;
    return a[0];
}

uint32_t OwnerGrid(Fake &, const uint32_t *) { return drawfx::P(&gw.grids[1]); }

uint32_t CameraPosition(Fake &, const uint32_t *a)
{
    static const float kNudges[] = {0.0f, 0.0f, 1e-4f, 5e-4f, 1e-3f, 2e-3f, 1.0f, -1.0f};
    gw.cam[gw.Next() % 3] += kNudges[gw.Next() % std::size(kNudges)];
    float *const out = Arg<float>(a[0]);
    out[0] = gw.cam[0], out[1] = gw.cam[1], out[2] = gw.cam[2];
    return a[0];
}
uint32_t CameraView(Fake &, const uint32_t *) { return drawfx::P(gw.buf.view); }

uint32_t PlacementInside(Fake &, const uint32_t *) { return Junk(gw.Next() % 3 != 0); }
uint32_t SingletonSlot28(Fake &, const uint32_t *)
{
    static const uint32_t kAnswers[] = {0, 1, 0x100, 2, 3, 3, 0x102};
    return kAnswers[gw.Next() % std::size(kAnswers)];
}
uint32_t AnimBone(Fake &, const uint32_t *) { return gw.Next() % 3 == 0 ? 0 : drawfx::P(&gw.bones[gw.Next() % kBones]); }

// ---- the subsets' elements (FloorTest's walk) ----

uint32_t ShapeLod(Fake &, const uint32_t *) { return gw.Next() % 3; }
// The part's mesh at a level: none at level 2 or now and then, else one of two.
uint32_t ShapePart(Fake &, const uint32_t *a)
{
    if (a[0] == 2 || gw.Next() % 5 == 0) return 0;
    return drawfx::P(&gw.meshes[(a[0] + gw.Next()) % 2]);
}
uint32_t MeshGeometry(Fake &, const uint32_t *) { return drawfx::P(&gw.geometry); }
// The cursor (hidden out): the walker, a mark (sometimes none) and the end, each counted; 0-4 elements to walk.
uint32_t MeshCursor(Fake &, const uint32_t *a)
{
    Fake **const cursor = Arg<Fake *>(a[0]);
    cursor[0] = &gw.walker;
    cursor[1] = gw.Next() % 3 == 0 ? nullptr : &gw.mark;
    cursor[2] = &gw.cursorEnd;
    for (unsigned i = 0; i < 3; ++i)
        if (cursor[i]) ++cursor[i]->refs;
    gw.walkerAt = 0;
    gw.walkerCount = gw.Next() % 5;
    return a[0];
}
uint32_t WalkerAtEnd(Fake &, const uint32_t *) { return Junk(gw.walkerAt >= gw.walkerCount); }
uint32_t WalkerIndex(Fake &, const uint32_t *) { return gw.Next() % 9; }
uint32_t WalkerStep(Fake &, const uint32_t *)
{
    ++gw.walkerAt;
    return Junk(0);
}
// An element's box (min, max at +0x1c): around the tiles' edges and centres, sometimes thin (under, at or over the
// 0.01 the tile span widens below), flat, inside out or NaN.
float Coordinate()
{
    static const float kBases[] = {-1.5f, -1.0f, -0.5f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.5f, 2.49f, 3.0f};
    return kBases[gw.Next() % std::size(kBases)];
}
uint32_t GeometryElement(Fake &, const uint32_t *a)
{
    static const float kWidths[] = {0.0f, 0.004f, 0.0099f, 0.01f, 0.0101f, 0.3f, 1.0f, 1.0f, 2.0f, 2.5f, 3.75f};
    uint8_t *const element = gw.buf.elements[a[0] % 4];
    float box[6];
    for (unsigned i = 0; i < 3; ++i) {
        box[i] = Coordinate();
        box[i + 3] = box[i] + kWidths[gw.Next() % std::size(kWidths)];
    }
    const uint32_t r = gw.Next() % 12;
    if (r == 0) box[gw.Next() % 6] = kNan;
    else if (r == 1) std::swap(box[0], box[3]);
    for (unsigned i = 0; i < 0x1c; ++i) element[i] = uint8_t(gw.Next());
    std::memcpy(element + 0x1c, box, sizeof box);
    return drawfx::P(element);
}
// The owner's position (through its buffer, now and then another): its height on a floor, between floors or NaN.
uint32_t OwnerPosition(Fake &, const uint32_t *a)
{
    static const float kHeights[] = {0.0f, 0.0f, 3.0f, 6.0f, 1.25f, -0.5f, kNan};
    float *const out = gw.Next() % 6 == 0 ? gw.buf.position : Arg<float>(a[0]);
    out[0] = Coordinate(), out[1] = Coordinate(), out[2] = kHeights[gw.Next() % std::size(kHeights)];
    return drawfx::P(out);
}
uint32_t LevelShownAnswer(Fake &, const uint32_t *) { return Junk(gw.Next() % 5 != 0); }
uint32_t EdgeTest(Fake &, const uint32_t *) { return Junk(gw.Next() % 7 != 0); }

// ---- stand-ins ----

void *__cdecl StandCameraOwner()
{
    drawfx::Note("camera owner", 0);
    return &gw.cameraOwner;
}
void *__cdecl StandPlacement()
{
    drawfx::Note("placement", 0);
    return &gw.placement;
}
void *__cdecl StandScene()
{
    drawfx::Note("scene", 0);
    return &gw.scene;
}
void __cdecl StandBindSubset(void *node, const char *material, const char *subset)
{
    drawfx::Note("bind subset", drawfx::Ptr(node));
    sgfx::NoteText("material", material);
    sgfx::NoteText("subset", subset);
}
void *__cdecl StandAnimated(void *node)
{
    drawfx::Note("animated face", drawfx::Ptr(node));
    return gw.Next() % 3 == 0 ? nullptr : &gw.animFace;
}
void *__cdecl StandNamed(const char *name, void *node, uint32_t mask)
{
    sgfx::NoteText("named node", name);
    drawfx::Note("named in", {drawfx::Ptr(node), mask});
    return gw.Next() % 3 == 0 ? &gw.bones[gw.Next() % kBones] : nullptr;
}
float Nudged(float f, int32_t steps) { return std::bit_cast<float>(std::bit_cast<int32_t>(f) + steps); }
// The bone's transform relative to the object: the last one again, nudged by a few float steps (either side of the
// 32-step match), its rotation negated, a NaN, or another base; its out, or another buffer.
const float *__cdecl StandRelative(float *out, void *bone, void *node)
{
    drawfx::Note("relative", {drawfx::Ptr(out), drawfx::Ptr(bone), drawfx::Ptr(node)});
    static const int32_t kSteps[] = {1, 31, 32, 33, 100, -1, -31, -32, -33, 0x40000};
    float *const last = gw.lastRelative;
    const uint32_t r = gw.Next() % 10;
    if (r == 0) {
        gw.boneBase = gw.Next() % 4;
        std::memcpy(last, kTransforms[gw.boneBase], sizeof(kTransforms[0]));
    } else if (r <= 3) {
        const unsigned k = gw.Next() % 7;
        last[k] = Nudged(last[k], kSteps[gw.Next() % std::size(kSteps)]);
    } else if (r == 4) {
        for (unsigned k = 3; k < 7; ++k) last[k] = -last[k];
    } else if (r == 5) {
        for (unsigned k = 3; k < 7; ++k) last[k] = -last[k];
        const unsigned k = 3 + gw.Next() % 4;
        last[k] = Nudged(last[k], kSteps[gw.Next() % std::size(kSteps)]);
    } else if (r == 6) {
        last[gw.Next() % 7] = kNan;
    } else if (r == 7) {
        std::memcpy(last, kTransforms[gw.boneBase], sizeof(kTransforms[0]));
    }
    std::memcpy(out, last, sizeof(gw.lastRelative));
    if (gw.Next() % 6 == 0) {
        std::memcpy(gw.buf.relative, last, sizeof(gw.buf.relative));
        gw.buf.relative[gw.Next() % 3] += 0.5f;
        return gw.buf.relative;
    }
    return out;
}
int32_t __cdecl StandTerrain(const int32_t *point)
{
    drawfx::Note("terrain", {uint32_t(point[0]), uint32_t(point[1]), uint32_t(point[2])});
    return int32_t(gw.Next() % 4);
}
int32_t __cdecl StandLevelOfGround(const int32_t *point)
{
    drawfx::Note("level of ground", {uint32_t(point[0]), uint32_t(point[1]), uint32_t(point[2])});
    static const int32_t kGroundLevels[] = {0, 0, 1, 2};
    return kGroundLevels[gw.Next() % std::size(kGroundLevels)];
}
uint32_t __cdecl StandQuadrant(const uint8_t *tile)
{
    drawfx::Note("quadrant", {W(tile, 0), W(tile, 4), W(tile, 8), tile[12]});
    return Junk(gw.Next() % 2);
}
void *__cdecl StandLevelShown()
{
    drawfx::Note("level shown", 0);
    return &gw.levelShown;
}
// ---- CreateResources' world ----

// The names CreateResources makes carry the shadow's address (shadowTexture%x, shadowMaterial%x), each side's own: the
// text just logged for the call gets it as "<shadow>".
uint32_t Unaddress(Fake &, const uint32_t *)
{
    difftest::NotCounted bookkeeping;
    if (!gw.shadow) return 0;
    char hex[12];
    std::snprintf(hex, sizeof hex, "%x", unsigned(drawfx::P(gw.shadow)));
    std::string &text = drawfx::gLog.back().text;
    for (size_t at; (at = text.find(hex)) != std::string::npos;) text.replace(at, std::strlen(hex), "<shadow>");
    return 0;
}

// The render target's texture (slot 6, on the target embedded in its block): its index (+0x10) set, -1 now and then.
uint32_t TargetTexture(Fake &self, const uint32_t *a)
{
    Unaddress(self, a);
    static const int32_t kIndices[] = {-1, 0, 3, 0x40, 7, 1};
    *reinterpret_cast<int32_t *>(Bytes(self.owner) + 0x10) = kIndices[gw.Next() % std::size(kIndices)];
    return 0;
}
// A grid's projector by name: the two projectors in turn.
uint32_t GridProjector(Fake &self, const uint32_t *a)
{
    Unaddress(self, a);
    return drawfx::P(&gw.projectors[gw.projectorsMade++ % 2]);
}
// The projector's slot 5: the camera's view (logged as a pointer), or a colour on the stack (its floats noted).
uint32_t ProjectorView(Fake &, const uint32_t *a)
{
    if (a[0] != drawfx::P(gw.buf.view)) {
        difftest::NotCounted bookkeeping;
        drawfx::Call c;
        c.note = "projector colour";
        drawfx::Words(c.words, Arg<void>(a[0]), 3, "u");
        drawfx::gLog.push_back(std::move(c));
    }
    return 0;
}

// The director's creates by class: the viewer, the transform nodes (our node, then the lift, in turn), the shape holder
// (the drawable), the quad's shape and the material definition; each counted.
uint32_t DirectorCreate(Fake &, const uint32_t *a)
{
    Fake *made = nullptr;
    switch (a[0]) {
    case 0xdca76dbbu: made = &gw.viewer; break;
    case 0x65246462u: made = gw.transforms++ % 2 == 0 ? &gw.node : &gw.lift; break;
    case 0x65245517u: made = &gw.drawable; break;
    case 0xfc6eb1f7u: made = &gw.quad; break;
    case 0x49596978u: made = &gw.material; break;
    default: return 0;
    }
    ++made->refs;
    *Arg<Fake *>(a[2]) = made;
    return 1;
}
uint32_t MaterialAdd(Fake &, const uint32_t *)
{
    static const int32_t kIndices[] = {0x10, 0x11, -1, 5, 0};
    return uint32_t(kIndices[gw.Next() % std::size(kIndices)]);
}
// A group's bit: past 31 too (the shift takes it mod 32).
uint32_t GroupBit(Fake &, const uint32_t *)
{
    static const uint32_t kBits[] = {0, 3, 5, 31, 32, 37, 0xffffffffu};
    return kBits[gw.Next() % std::size(kBits)];
}

// The filter tried on a node of the trees (Accept, slot 3, through its own table), or asked for an interface.
void TryFilter(uint8_t *filter)
{
    void *const nodes[] = {&gw.objects[0], &gw.objects[1], &gw.kids[0], &gw.kids[2], &gw.kids[3], &gw.viewerKids[1]};
    if (gw.Next() % 4 == 0) {
        static const uint32_t kIids[] = {1u, 0x5ca76db6u, 0u};
        const uint32_t iid = kIids[gw.Next() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        const bool found = Virtual<0, uint8_t>(filter, iid, &out) != 0;
        drawfx::Note("filter query", {iid, uint32_t(found), found ? uint32_t(Bytes(out) - filter) : 0x5a5au});
        if (found) drawfx::Note("filter query release", Virtual<2, uint32_t>(out));
        return;
    }
    void *const node = nodes[gw.Next() % std::size(nodes)];
    drawfx::Note("accept", {drawfx::Ptr(node), uint32_t(Virtual<3, uint8_t>(filter, node, &gw.viewer))});
}
// The viewer's filter (slot 103): its words noted (the count, the id's low half, the mask), held (the one held before
// released), and tried.
uint32_t ViewerFilter(Fake &, const uint32_t *a)
{
    uint8_t *const filter = Arg<uint8_t>(a[0]);
    drawfx::Note("filter", {W(filter, 8), W(filter, 0xc) & 0xffff, W(filter, 0x10)});
    drawfx::Note("filter addref", Virtual<1, uint32_t>(filter));
    if (gw.filter) drawfx::Note("filter released", Virtual<2, uint32_t>(gw.filter));
    gw.filter = filter;
    for (unsigned i = 0, n = gw.Next() % 4; i < n; ++i) TryFilter(filter);
    return 0;
}

// nTSSG::cBlurredRenderTarget's constructor 0x3c9ed0 (thiscall, returns this): the target fake embedded in the block,
// the rest zeroed, the index -1.
void *__fastcall StandTarget(uint8_t *block, void *)
{
    drawfx::Note("render target", drawfx::Ptr(block));
    std::memset(block, 0, 0x18);
    {
        difftest::NotCounted bookkeeping; // the fakes' record of embeddings (it grows on one side only)
        gw.target.Embed(block);
    }
    *reinterpret_cast<int32_t *>(block + 0x10) = -1;
    return block;
}
// The quad's geometry 0xaf16f0 (__cdecl, ours nGZSceneGraph::MakeQuadMesh): the centre, the size and the texture
// corners noted; the geometry handed out (counted); out returned, as both makers do.
void **__cdecl StandQuad(void **out, const float *corner, const float *size, const float *uvLow, const float *uvHigh)
{
    {
        difftest::NotCounted bookkeeping;
        drawfx::Call c;
        c.note = "quad";
        drawfx::Words(c.words, corner, 3, "u");
        drawfx::Words(c.words, size, 2, "u");
        drawfx::Words(c.words, uvLow, 2, "u");
        drawfx::Words(c.words, uvHigh, 2, "u");
        drawfx::gLog.push_back(std::move(c));
    }
    ++gw.quadGeometry.refs;
    *out = &gw.quadGeometry;
    return out;
}
void *__cdecl StandDirector()
{
    drawfx::Note("director", 0);
    return &gw.director;
}
nGZSceneGraph::cIRenderGroupRegistry *__cdecl StandGroups()
{
    drawfx::Note("render groups", 0);
    return reinterpret_cast<nGZSceneGraph::cIRenderGroupRegistry *>(&gw.groups);
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::CodePatch cameraOwner{0x20defbu, shape::CodeOf(&StandCameraOwner)};
    shape::CodePatch placement{0x20e0b2u, shape::CodeOf(&StandPlacement)};
    shape::CodePatch scene{0x211ce1u, shape::CodeOf(&StandScene)};
    shape::CodePatch bindSubset{0x3ad4a0u, shape::CodeOf(&StandBindSubset)};
    shape::StandIn animated{0xae1ed0u, &nGZSceneGraph::AnimatedFaceOf, &StandAnimated};
    shape::StandIn named{0xae7490u, &nGZSceneGraph::FindNodeByName, &StandNamed};
    shape::StandIn relative{0xaeb2c0u, &nGZSceneGraph::RelativeTransform, &StandRelative};
    shape::CodePatch terrain{0x7e2cc0u, shape::CodeOf(&StandTerrain)};
    shape::CodePatch levelOfGround{0x7e28d0u, shape::CodeOf(&StandLevelOfGround)};
    shape::CodePatch quadrant{0x7e0920u, shape::CodeOf(&StandQuadrant)};
    shape::CodePatch levelShown{0x20df51u, shape::CodeOf(&StandLevelShown)};
    shape::CodePatch target{0x3c9ed0u, shape::CodeOf(&StandTarget)};
    shape::StandIn quad{0xaf16f0u, &nGZSceneGraph::MakeQuadMesh, &StandQuad};
    shape::StandIn director{0x1ae18du, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn groups{0xaeb370u, &nGZSceneGraph::RenderGroupRegistry, &StandGroups};    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// ---- the world ----

// The record's blur radii ([3] indoors, [4] outdoors) and the view's W0 H0 W1 H1 ([7..10]): equal or not, negative,
// zero, sometimes NaN. Each NaN has its own payload, unlike the boxes' kNan, and half the NaN records put one in W0 or
// H0, the only two whose NaN reaches the extents (SettingsExtent's comparisons drop W1's and H1's): mode 1's sums in
// PlaceProjectors then meet a box's NaN, where operand order shows.
void SettingsValues()
{
    static const float kSpans[] = {1.0f, 2.0f, 0.5f, 3.0f, 2.0f, 0.0f, -1.0f, 1.5f, -0.0f, 4.0f, kNan};
    // The size (mode 1's quad; NaN with its own payload too) and the resolution (under twice the blur radius too).
    static const float kSizes[] = {0.5f, 1.0f, 2.0f, 0.0f, -1.0f, 3.25f, kNan};
    float size = kSizes[gw.Next() % std::size(kSizes)];
    if (std::isnan(size)) size = std::bit_cast<float>(0x7fc00000u | (1u + gw.Next() % 0x3ffffeu));
    gw.buf.record[1] = std::bit_cast<int32_t>(size);
    static const uint32_t kResolutions[] = {0, 1, 3, 8, 64, 128, 256, 256};
    gw.buf.record[5] = int32_t(kResolutions[gw.Next() % std::size(kResolutions)]);
    gw.buf.record[3] = int32_t(gw.Next() % 5);
    gw.buf.record[4] = int32_t(gw.Next() % 5);
    const bool nan = gw.Next() % 4 == 0;
    for (unsigned i = 7; i < 11; ++i) {
        float v = kSpans[gw.Next() % (std::size(kSpans) - (nan ? 0 : 1))];
        if (std::isnan(v)) v = std::bit_cast<float>(0xffc00000u | (1u + gw.Next() % 0x3ffffeu));
        gw.buf.record[i] = std::bit_cast<int32_t>(v);
    }
    if (nan && gw.Next() % 2)
        gw.buf.record[7 + gw.Next() % 2] = int32_t(0x7fc00000u | (1u + gw.Next() % 0x3ffffeu));
    if (gw.Next() % 4 == 0) gw.buf.record[9] = gw.buf.record[7]; // W1 = W0
}

uint32_t OptionBits()
{
    uint32_t bits = 0;
    for (const uint32_t bit : {0u, 2u, 4u, 5u, 6u, 7u, 9u})
        if (gw.Next() % 2) bits |= 1u << bit;
    for (const uint32_t bit : {1u, 3u})
        if (gw.Next() % 4 != 0) bits |= 1u << bit;
    if (gw.Next() % 4 == 0) bits |= 1u << 8; // debug
    return bits;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 11;
    for (unsigned i = 0; i < kObjects; ++i) {
        Holder &h = gw.objects[i];
        MakeNode(gw.objectFaces[i], 600 + i);
        h.unknown.Reset<kUnknownFace>(605 + i);
        gw.objectFaces[i].Embed(&h);
        h.vbptr = kHolderBases;
        h.pad[0] = h.pad[1] = 0;
    }
    drawfx::AddRange(gw.objects, sizeof gw.objects, kObjectTag);
    for (unsigned i = 0; i < kKids; ++i) MakeNode(gw.kids[i], 610 + i);
    Attach(gw.objectFaces[0], gw.kids[1]);
    Attach(gw.objectFaces[0], gw.kids[0]);
    Attach(gw.kids[1], gw.kids[2]);
    Attach(gw.objectFaces[1], gw.kids[3]);
    for (unsigned i = 0; i < kHolders; ++i) MakeNode(gw.holders[i], 620 + i);
    for (unsigned i = 0; i < kShapes; ++i) {
        gw.shapes[i].Reset<kShape>(625 + i);
        gw.shapes[i].On(47, &ShapePart);
        gw.shapes[i].On(53, &ShapeMaterial);
        gw.shapes[i].On(57, &ShapeLod);
        gw.shapeList[i] = &gw.shapes[(i + seed) % kShapes];
    }
    gw.owner.Reset<kOwner>(630);
    gw.owner.On(13, &OwnerFlag);
    gw.owner.On(42, &OwnerPosition);
    gw.owner.On(43, &OwnerId);
    gw.owner.On(47, &OwnerFilterId);
    gw.owner.On(98, &OwnerKind);
    gw.owner.On(144, &OwnerTest);
    gw.owner.On(189, &OwnerTest);
    gw.manager.Reset<kManager>(631);
    gw.manager.On(8, &ManagerAcquire);
    gw.manager.On(12, &ManagerChange);
    gw.manager.On(13, &ManagerTexture);
    gw.manager.On(14, &ManagerMatrix);
    gw.manager.On(15, &ManagerScale);
    gw.manager.On(16, &ManagerBox);
    gw.cameraOwner.Reset<kCameraOwner>(632);
    gw.cameraOwner.Returns(38, static_cast<const void *>(&gw.levels));
    gw.cameraOwner.On(45, &OwnerGrid);
    gw.cameraOwner.Returns(49, static_cast<const void *>(&gw.camera));
    gw.cameraOwner.Returns(50, static_cast<const void *>(&gw.manager));
    gw.levels.Reset<kLevels>(633);
    gw.levels.Returns(19, static_cast<const void *>(&gw.level));
    gw.level.Reset<kLevel>(634);
    gw.level.Returns(25, static_cast<const void *>(&gw.grids[2]));
    for (unsigned i = 0; i < kGrids; ++i) gw.grids[i].Reset<kGrid>(635 + i);
    gw.camera.Reset<kCamera>(640);
    gw.camera.On(30, &CameraPosition);
    gw.camera.On(31, &CameraView);
    for (unsigned i = 0; i < 2; ++i) {
        gw.projectors[i].Reset<kProjector>(641 + i);
        gw.projectors[i].On(5, &ProjectorView);
    }
    for (unsigned i = 0; i < kGrids; ++i) gw.grids[i].On(8, &GridProjector);
    MakeNode(gw.drawable, 643);
    gw.viewer.Reset<kViewer>(644);
    gw.viewer.On(34, &TreeParent);
    gw.viewer.On(38, &TreeSibling);
    gw.viewer.On(42, &TreeChildren);
    gw.viewer.On(48, &TreeFlag);
    gw.viewer.On(54, &TreeWorld);
    gw.viewer.On(103, &ViewerFilter);
    gw.viewer.On(120, &TreeOwner);
    gw.viewer.On(139, &ViewerEntry);
    gw.viewer.On(140, &ViewerView);
    gw.viewer.On(143, &ViewerClip);
    for (unsigned i = 0; i < 2; ++i) MakeNode(gw.viewerKids[i], 677 + i);
    Attach(gw.viewer, gw.viewerKids[0]);
    Attach(gw.viewerKids[0], gw.viewerKids[1]);
    gw.target.Reset<kTarget>(645);
    gw.target.On(6, &TargetTexture);
    gw.director.Reset<kDirector>(670);
    gw.director.On(3, &DirectorCreate);
    MakeNode(gw.lift, 671);
    gw.quad.Reset<kShape>(672);
    gw.quad.On(34, &Unaddress);
    gw.quadGeometry.Reset<kUnknownFace>(673);
    gw.material.Reset<kMaterialDef>(674);
    gw.material.On(10, &Unaddress);
    gw.material.On(13, &Unaddress);
    gw.materials.Reset<kMaterials>(675);
    gw.materials.On(23, &MaterialAdd);
    gw.groups.Reset<kGroups>(676);
    gw.groups.On(3, &GroupBit);
    gw.transforms = gw.projectorsMade = 0;
    gw.shadow = nullptr;
    gw.filter = nullptr;
    MakeNode(gw.node, 646);
    MakeNode(gw.root, 647);
    gw.scene.Reset<kScene>(648);
    gw.scene.Returns(17, static_cast<const void *>(&gw.root));
    gw.placement.Reset<kPlacement>(649);
    gw.placement.On(72, &PlacementInside);
    gw.placement.Returns(39, static_cast<const void *>(&gw.walls));
    gw.walls.Reset<kWalls>(660);
    gw.walls.On(44, &EdgeTest);
    gw.levelShown.Reset<kLevelShown>(661);
    gw.levelShown.On(22, &LevelShownAnswer);
    for (unsigned i = 0; i < 2; ++i) {
        gw.meshes[i].Reset<kMesh>(662 + i);
        gw.meshes[i].On(29, &MeshGeometry);
        gw.meshes[i].On(44, &MeshCursor);
    }
    gw.walker.Reset<kWalker>(664);
    gw.walker.On(4, &WalkerAtEnd);
    gw.walker.On(5, &WalkerIndex);
    gw.walker.On(6, &WalkerStep);
    gw.mark.Reset<kUnknownFace>(665);
    gw.cursorEnd.Reset<kUnknownFace>(666);
    gw.geometry.Reset<kGeometry>(667);
    gw.geometry.On(5, &GeometryElement);
    gw.walkerAt = gw.walkerCount = 0;
    gw.singleton.Reset<kSingleton>(650);
    gw.singleton.On(28, &SingletonSlot28);
    gw.animFace.Reset<kAnimFace>(651);
    gw.animFace.On(52, &AnimBone);
    for (unsigned i = 0; i < kBones; ++i) {
        gw.bones[i].Reset<kBone>(655 + i);
        gw.bones[i].On(54, &TreeWorld);
    }
    std::memset(&gw.buf, 0, sizeof gw.buf);
    gw.buf.record[0] = int32_t(seed % 3);
    gw.buf.record[11] = int32_t(OptionBits());
    SettingsValues();
    for (unsigned i = 0; i < 16; ++i) gw.buf.view[i] = float(i) * 0.25f;
    drawfx::AddRange(&gw.buf, sizeof gw.buf, kBufferTag);
    gw.cam[0] = 1.0f, gw.cam[1] = -2.0f, gw.cam[2] = float(seed % 3);
    gw.boneBase = seed % 4;
    std::memcpy(gw.lastRelative, kTransforms[gw.boneBase], sizeof gw.lastRelative);
    static const int32_t kOwnerIds[] = {8, 0, 1, 3};
    gw.ownerId = kOwnerIds[seed % 4];
    gw.ownerNull = false;
    cells.Set(kSingletonCell, &gw.singleton);
    cells.Set(kMaterialsCell, &gw.materials);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.owner, &gw.manager, &gw.cameraOwner, &gw.levels, &gw.level, &gw.camera, &gw.drawable, &gw.viewer,
                    &gw.target, &gw.node, &gw.root, &gw.scene, &gw.placement, &gw.singleton, &gw.animFace, &gw.walls,
                    &gw.levelShown, &gw.walker, &gw.mark, &gw.cursorEnd, &gw.geometry, &gw.meshes[0], &gw.meshes[1],
                    &gw.director, &gw.lift, &gw.quad, &gw.quadGeometry, &gw.material, &gw.materials, &gw.groups,
                    &gw.viewerKids[0], &gw.viewerKids[1]})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.objectFaces) c.words.push_back(uint32_t(f.refs));
    for (const Holder &h : gw.objects) c.words.push_back(uint32_t(h.unknown.refs));
    for (const Fake &f : gw.kids) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.holders) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.shapes) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.grids) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.projectors) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.bones) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

// Each word's kind: the vtable and the strings' vtables and buffers left out (each text noted on its own); the object,
// the resources and the grids strict pointers.
const char *ObjectKinds()
{
    static char kinds[kWords + 1];
    if (kinds[0]) return kinds;
    std::memset(kinds, 'u', kWords);
    kinds[0] = '-';
    for (unsigned w : {8u, 9u, 10u, 11u, 12u, 17u, 18u, 19u}) kinds[w] = 'a';
    for (unsigned s = 0; s < cShadow::kSubsets; ++s)
        for (unsigned w = 0; w < 5; ++w) kinds[0x64 / 4 + s * 8 + w] = '-';
    return kinds;
}

void NoteObject(void *o)
{
    difftest::NotCounted bookkeeping;
    static char labels[11][20];
    for (unsigned g = 0; g * 8 < kWords; ++g) {
        if (!labels[g][0]) std::snprintf(labels[g], sizeof labels[g], "object +0x%02x", g * 32);
        const unsigned first = g * 8, count = kWords - first < 8 ? kWords - first : 8;
        char kinds[9] = {};
        std::memcpy(kinds, ObjectKinds() + first, count);
        drawfx::Call c;
        c.note = labels[g];
        drawfx::Words(c.words, Bytes(o) + first * 4, count, kinds);
        drawfx::gLog.push_back(std::move(c));
    }
    for (unsigned s = 0; s < cShadow::kSubsets; ++s)
        sgfx::NoteText("material text", reinterpret_cast<const nGZLog::LogString *>(Bytes(o) + 0x64 + s * 0x20)->mString.c_str());
}

struct Run {
    void *o;

    cIGZUnknown *Object(std::mt19937 &rng) { return reinterpret_cast<cIGZUnknown *>(&gw.objects[rng() % kObjects]); }
    bool HasObject() const { return W(o, 0x20) != 0; }

    void Query(std::mt19937 &rng)
    {
        static const uint32_t kIids[] = {1u, cShadow::kIid, cShadow::kClass, 0u, 0x4aa7ec1bu};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        void **const where = rng() % 6 == 0 ? nullptr : &out;
        const bool found = Virtual<0, uint8_t>(o, iid, where) != 0;
        drawfx::Note("query", {iid, uint32_t(found), found && where ? drawfx::Ptr(out) : 0x5a5a, W(o, 4)});
        if (found) Virtual<2, uint32_t>(o);
    }
    void WorldChange(std::mt19937 &rng)
    {
        const uint32_t r = rng() % 4;
        if (r == 0) {
            ++gw.buf.record[0];
        } else if (r == 1) {
            gw.buf.record[11] = int32_t(OptionBits());
        } else if (r == 2) {
            ++gw.buf.record[0];
            gw.buf.record[11] = int32_t(OptionBits());
            SettingsValues();
        } else {
            static const int32_t kOwnerIds[] = {8, 0, 1, 3};
            gw.ownerId = kOwnerIds[rng() % std::size(kOwnerIds)];
        }
        drawfx::Note("world", {uint32_t(gw.buf.record[0]), uint32_t(gw.buf.record[11]), uint32_t(gw.ownerId)});
    }
    void SetMode(std::mt19937 &rng)
    {
        static const int32_t kModes[] = {0, 1, 2, 2, 3, 4, 5};
        const int32_t mode = kModes[rng() % std::size(kModes)];
        // The owner's null test (SelectMode's) only where nothing after it reads the owner: the materials not bound, and
        // no resources made for modes 1 and 4 (CreateResources reads the object's owner for the filter).
        gw.ownerNull = !Bytes(o)[9] && (!Bytes(o)[0xa] || (mode != 1 && mode != 4)) && rng() % 3 == 0;
        Virtual<6, void>(o, mode);
        gw.ownerNull = false;
    }

    void Step(std::mt19937 &rng)
    {
        const unsigned what = rng() % 48;
        if (what < 2) {
            drawfx::Note("init", Virtual<3, uint8_t>(o));
        } else if (what < 3) {
            drawfx::Note("shutdown", Virtual<4, uint8_t>(o));
        } else if (what < 6) {
            // The original reads the object's visibility without a null test (no null object here).
            Virtual<5, void>(o, Object(rng));
        } else if (what < 9) {
            if (!HasObject() && Bytes(o)[0xa]) return;
            SetMode(rng);
        } else if (what < 11) {
            static const uint32_t kOn[] = {0, 1, 2, 0x100, 0x101};
            if (!HasObject()) return; // the visibility read
            Virtual<7, void>(o, kOn[rng() % std::size(kOn)]);
        } else if (what < 12) {
            Virtual<8, void>(o);
        } else if (what < 13) {
            if (!HasObject()) return; // the materials read from the object
            Virtual<9, void>(o);
        } else if (what < 15) {
            if (!HasObject()) return; // the visibility read
            Virtual<10, void>(o);
        } else if (what < 16) {
            Virtual<11, void>(o);
        } else if (what < 30) {
            Virtual<12, void>(o);
        } else if (what < 32) {
            Virtual<13, void>(o);
        } else if (what < 33) {
            Virtual<14, void>(o);
        } else if (what < 34) {
            Virtual<15, void>(o);
        } else if (what < 35) {
            Virtual<16, void>(o);
        } else if (what < 36) {
            Virtual<17, void>(o);
        } else if (what < 37) {
            drawfx::Note("getters", {Virtual<18, uint8_t>(o), Virtual<19, uint8_t>(o), Virtual<20, uint32_t>(o),
                                     Virtual<21, uint32_t>(o)});
        } else if (what < 38) {
            const uint32_t subset = rng() % cShadow::kSubsets;
            drawfx::Note("has material", {subset, Virtual<22, uint8_t>(o, subset)});
        } else if (what < 39) {
            Query(rng);
        } else if (what < 40) {
            drawfx::Note("addref, release", {Virtual<1, uint32_t>(o), Virtual<2, uint32_t>(o)});
        } else {
            WorldChange(rng);
        }
    }
};

void RunLife(difftest::Context &ctx, uint32_t seed, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *o;
    if (seed % 3 == 0) {
        o = original ? ctx.Original<void *(__cdecl *)()>(0x355700)() : static_cast<void *>(cShadow::Create());
        // The bytes the constructor leaves unwritten (the padding, the camera), set alike.
        for (uint32_t at = 0xd; at < 0x10; ++at) Bytes(o)[at] = uint8_t(rng());
        for (uint32_t at = 0x34; at < 0x40; ++at) Bytes(o)[at] = uint8_t(rng() % 3 == 0 ? 0 : rng());
        for (uint32_t at = 0x14e; at < 0x150; ++at) Bytes(o)[at] = uint8_t(rng());
    } else {
        o = ::operator new(kSize);
        for (uint32_t i = 0; i < kSize; ++i) Bytes(o)[i] = uint8_t(rng());
        if (original) ctx.Original<void *(__thiscall *)(void *)>(0x354320)(o);
        else cShadow::Construct(o, nullptr);
    }
    drawfx::Note("made", drawfx::Ptr(o));
    gw.shadow = o;
    Run run{o};
    NoteObject(o);
    Virtual<1, uint32_t>(o);
    if (rng() % 4 != 0) {
        sgfx::MarkStep(ctx, "bring up");
        drawfx::Note("init", Virtual<3, uint8_t>(o));
        Virtual<5, void>(o, run.Object(rng));
        if (rng() % 3 != 0) Virtual<6, void>(o, int32_t(rng() % 2 ? 2 : rng() % 5));
        if (rng() % 4 != 0) Virtual<7, void>(o, uint32_t(1));
        if (rng() % 4 != 0) Virtual<10, void>(o);
        NoteObject(o);
    }
    const unsigned steps = 4 + rng() % 40;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        run.Step(rng);
        NoteObject(o);    }
    sgfx::MarkStep(ctx, "delete");
    if (rng() % 2) {
        drawfx::Note("released", Virtual<2, uint32_t>(o));
    } else {
        void *const result = Virtual<23, void *>(o, 1u); // receiver: the deleting destructor
        drawfx::Note("deleted", uint32_t(Bytes(result) - Bytes(o)));
    }
    gw.shadow = nullptr;
    if (gw.filter) { // the viewer's hold on its filter given up (the last reference: the filter deleted)
        drawfx::Note("filter last release", Virtual<2, uint32_t>(gw.filter));
        gw.filter = nullptr;
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the factory 0x355700, the constructor 0x354320, the destructor 0x354480 with the deleting destructor
// 0x3545b0 (slot 23), QueryInterface 0x356880, Init 0x355ab0, Shutdown 0x3569e0, SetObject 0x356950, SetMode 0x3569c0,
// SetEnabled 0x356930, Suspend 0x356b10, Resume 0x3568b0, Show 0x3545e0, Hide 0x3553b0, Update 0x355d20, Refresh
// 0x355be0, ApplyMaterials 0x356cc0 (and slot 17's 0x355ce0), RestoreMaterials 0x355cf0, HasMaterial 0x355a90, and the
// helpers CreateResources 0x354790 (with SwapLayerBitsDeep 0x354630, AddShadowMaterial 0x3546b0, and the filter it
// makes: cObjectShadowFilter's Accept 0x356a70, QueryInterface / AddRef / Release and its deleting destructor, as the
// viewer fake tries, holds and releases it), ReleaseResources 0x355290, UnbindMaterials 0x356a20, BindMaterials
// 0x355b50, BindSubset 0x355410, UpdateLevel 0x357750 (reached through CreateResources), UpdateVisibility 0x357d70,
// SelectMode 0x356b40, ModeChanged 0x3568d0, FollowsCamera 0x355a70, ObjectVisible 0x355bd0, UpdateSettings 0x356e40
// with PointInView 0x3ad430, PlaceProjectors 0x357810, ShadowObjectRef's release, with ObjectBounds,
// FindSubsetMaterial, SubsetMaterial, ShadowFootprint and ClipPlanesMeet as Update, PlaceProjectors and BindMaterials
// reach them.
DIFF_TEST(sg_shadow_life, 0x355d20)
{
    Stands stands;
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "shadow", 2000,
        [&](uint32_t seed, drawfx::Cells &cells) {
            current = seed;
            Build(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, current, original, rng);
            ++ctx.cases;
        });
}

namespace {

float Element(std::mt19937 &rng)
{
    static const float kValues[] = {0.0f,  1.0f,  -1.0f, 0.5f,  -0.5f, 2.0f,    1e-4f,      1e-3f,
                                    1.0000001e-3f, 3e-4f, 0.25f, -2.0f, 1e-7f, -0.0f, 9.999999e-4f, kNan};
    return kValues[rng() % std::size(kValues)];
}

// A NaN with its own payload (signalling or quiet, either sign), planted in pairs so that operand order shows: of two
// NaNs an SSE add or product keeps its first operand's.
float PayloadNaN(std::mt19937 &rng)
{
    const uint32_t bits = (rng() & 0x80000000u) | 0x7f800000u | (rng() % 2 ? 0x400000u : 0u) | (1u + rng() % 0x3ffffeu);
    return std::bit_cast<float>(bits);
}

void RunFootprint(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    for (unsigned i = 0; i < 64; ++i) {
        sgfx::MarkStep(ctx, "case");
        if (rng() % 3 == 0) {
            float a[4], b[4], out[2] = {-7.0f, -7.0f};
            for (float &f : a) f = Element(rng);
            for (float &f : b) f = Element(rng);
            if (rng() % 2) { // a NaN pair, mostly in the offsets (a NaN in x or y fails the determinant)
                a[rng() % 3 ? 3 : rng() % 4] = PayloadNaN(rng);
                b[rng() % 3 ? 3 : rng() % 4] = PayloadNaN(rng);
            }
            using Fn = uint8_t(__cdecl *)(const float *, const float *, float *);
            const uint8_t r = original ? ctx.Original<Fn>(0x3ad380)(a, b, out) : ClipPlanesMeet(a, b, out);
            drawfx::Call c;
            c.note = "planes meet";
            c.words = {r};
            drawfx::Words(c.words, a, 4, "u");
            drawfx::Words(c.words, b, 4, "u");
            drawfx::Words(c.words, out, 2, "u");
            drawfx::gLog.push_back(std::move(c));
            continue;
        }
        float m[16];
        const uint32_t kind = rng() % 4;
        if (kind < 2) {
            std::memcpy(m, kProjections[rng() % 6], sizeof m);
            if (kind == 1) m[rng() % 16] = Element(rng);
        } else {
            for (float &f : m) f = rng() % 3 == 0 ? Element(rng) : 0.0f;
            if (kind == 3) m[0] = m[5] = m[15] = 0.5f;
        }
        if (rng() % 3 == 0) { // a NaN pair in one column (mostly the offsets'), two rows
            const unsigned column = rng() % 2 ? 3 : rng() % 4;
            m[column + 4 * (rng() % 4)] = PayloadNaN(rng);
            m[column + 4 * (rng() % 4)] = PayloadNaN(rng);
        }
        float low[2] = {-9.0f, -9.0f}, high[2] = {9.0f, 9.0f};
        using Fn = uint8_t(__cdecl *)(const float *, float *, float *);
        const uint8_t r = original ? ctx.Original<Fn>(0x3aced0)(m, low, high) : ShadowFootprint(m, low, high);
        drawfx::Call c;
        c.note = "footprint";
        c.words = {r};
        drawfx::Words(c.words, m, 16, "u");
        drawfx::Words(c.words, low, 2, "u");
        drawfx::Words(c.words, high, 2, "u");
        drawfx::gLog.push_back(std::move(c));
    }
}

} // namespace

// Verifies: ClipPlanesMeet 0x3ad380 and ShadowFootprint 0x3aced0 called directly.
DIFF_TEST(sg_shadow_footprint, 0x3aced0)
{
    sgfx::CheckSides(
        ctx, "shadow footprint", 300, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunFootprint(ctx, original, rng);
            ++ctx.cases;
        });
}

namespace {

constexpr uint32_t kFilterTable = 0xdba368, kFilterUnknownTable = 0xdba37c;

// A cObjectShadowFilter built in a block of the test's: the image's (its two vtables, the count, the id over a junk
// padding half, the mask) or ours (constructed in place, then the same words set).
uint8_t *MakeFilter(difftest::Context &ctx, bool original, uint32_t count, uint32_t idWord, uint32_t mask)
{
    uint8_t *const block = static_cast<uint8_t *>(::operator new(0x14));
    if (original) {
        reinterpret_cast<void **>(block)[0] = ctx.image.At(kFilterTable);
        reinterpret_cast<void **>(block)[1] = ctx.image.At(kFilterUnknownTable);
        reinterpret_cast<uint32_t *>(block)[4] = mask;
    } else {
        new (block) cObjectShadowFilter(mask);
    }
    reinterpret_cast<uint32_t *>(block)[2] = count;
    reinterpret_cast<uint32_t *>(block)[3] = idWord;
    return block;
}

// The filter: Accept (slot 3) on nodes of the trees, then the deleting destructor on vtable words made junk: the body
// 0x354580 or the +4 table's thunk 0x3545a3 (+4 table slot 3) on the image's side, our +4 table's slot 3 on ours.
void RunFilter(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    static const uint32_t kMasks[] = {0xffffffffu, 0u, 1u << 3, 0x80000000u, 0x12345678u, 1u};
    const uint32_t mask = kMasks[rng() % std::size(kMasks)];
    const uint32_t idWord = (rng() << 16) | (rng() % 3 == 0 ? rng() % 3 : rng() & 0xffff);
    const uint32_t count = rng() % 3;
    uint8_t *const filter = MakeFilter(ctx, original, count, idWord, mask);
    void *const nodes[] = {&gw.objects[0], &gw.objects[1], &gw.kids[0],       &gw.kids[1],
                           &gw.kids[2],    &gw.kids[3],    &gw.viewerKids[0], &gw.viewerKids[1]};
    for (unsigned i = 0, n = 1 + rng() % 4; i < n; ++i) {
        void *const node = nodes[rng() % std::size(nodes)];
        drawfx::Note("accept", {drawfx::Ptr(node), uint32_t(Virtual<3, uint8_t>(filter, node, &gw.viewer))});
    }

    static const uint32_t kFlags[] = {0u, 1u, 0x100u, 0x101u, 3u, 2u};
    const uint32_t flags = kFlags[rng() % std::size(kFlags)];
    const bool viaThunk = rng() % 2 != 0;
    const uint32_t junk[2] = {rng(), rng()};
    void *ourDeleting = nullptr;
    if (!original) {
        cObjectShadowFilter prototype(0);
        ourDeleting = (*reinterpret_cast<void *const *const *>(reinterpret_cast<uint8_t *>(&prototype) + 4))[3];
    }
    std::memcpy(filter, junk, sizeof junk);
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    shape::DeleteRecorder deletes;
    deletes.Begin();
    void *result;
    if (original) {
        result = viaThunk ? ctx.Original<Deleting>(0x3545a3)(filter + 4, flags) : ctx.Original<Deleting>(0x354580)(filter, flags);
    } else {
        result = reinterpret_cast<Deleting>(ourDeleting)(filter + 4, flags); // receiver: our +4 table's slot 3 (the thunk)
    }
    deletes.End();
    drawfx::Call c;
    c.note = "filter deleted";
    c.words = {flags, uint32_t(Bytes(result) - filter), uint32_t(deletes.Count()), uint32_t(deletes.Is(0, filter, 0x14))};
    drawfx::Words(c.words, filter + 8, 3, "uuu");
    drawfx::gLog.push_back(std::move(c));
    deletes.Release();
    if (!(flags & 1)) ::operator delete(filter);
}

void RunHelpers(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    cIGZUnknown *const nodes[] = {reinterpret_cast<cIGZUnknown *>(&gw.objects[0]), reinterpret_cast<cIGZUnknown *>(&gw.objects[1]),
                                  reinterpret_cast<cIGZUnknown *>(&gw.kids[1]), reinterpret_cast<cIGZUnknown *>(&gw.kids[2])};
    static const char *const kSubsets[] = {"groundshadow", "wallshadow", "eastwallshadow", "other", ""};
    const unsigned steps = 4 + rng() % 24;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        cIGZUnknown *const node = nodes[rng() % std::size(nodes)];
        const unsigned what = rng() % 7;
        if (what == 3) {
            // The render-group bits swapped down a tree (the objects' or the viewer's).
            static const uint32_t kMasks[] = {0xffffffffu, 0u, 1u << 3, 0x80000000u, 0x12345678u};
            const uint32_t remove = kMasks[rng() % std::size(kMasks)], add = kMasks[rng() % std::size(kMasks)];
            cIGZUnknown *const top = rng() % 2 ? node : reinterpret_cast<cIGZUnknown *>(&gw.viewer);
            using Fn = void(__cdecl *)(cIGZUnknown *, uint32_t, uint32_t);
            if (original) ctx.Original<Fn>(0x354630)(top, remove, add);
            else SwapLayerBitsDeep(top, remove, add);
            continue;
        }
        if (what == 4) {
            // A material: the debug flag a byte over a junk upper part.
            static const char *const kNames[] = {"shadowMaterial1", "m", ""};
            static const char *const kTextures[] = {"shadowTexture1", "t"};
            static const char *const kShaders[] = {"ShadowShader", "Other"};
            static const uint32_t kDebug[] = {0u, 1u, 0x100u, 0x2ff00u, 0x301u};
            const char *const name = kNames[rng() % std::size(kNames)];
            const char *const texture = kTextures[rng() % std::size(kTextures)];
            const char *const shader = kShaders[rng() % std::size(kShaders)];
            const uint32_t debug = kDebug[rng() % std::size(kDebug)];
            using Fn = int32_t(__cdecl *)(const char *, const char *, const char *, uint32_t);
            const Fn ours = reinterpret_cast<Fn>(&AddShadowMaterial); // the byte argument passed with its junk part
            drawfx::Note("material", uint32_t(original ? ctx.Original<Fn>(0x3546b0)(name, texture, shader, debug)
                                                       : ours(name, texture, shader, debug)));
            continue;
        }
        if (what >= 5) {
            RunFilter(ctx, original, rng);
            continue;
        }
        if (what == 0) {
            static const int32_t kMasks[] = {-1, 0, 4, 3, -2};
            const int32_t mask = kMasks[rng() % std::size(kMasks)];
            float box[6];
            const uint32_t r = rng() % 3;
            for (unsigned k = 0; k < 6; ++k)
                box[k] = r == 0 ? (k < 3 ? 3.4e38f : -3.4e38f) : r == 1 ? kNan : float(int32_t(rng() % 5) - 2);
            using Fn = void(__cdecl *)(float *, int32_t, cIGZUnknown *);
            if (original) ctx.Original<Fn>(0x3558d0)(box, mask, node);
            else ObjectBounds(box, mask, node);
            drawfx::Call c;
            c.note = "bounds";
            c.words = {uint32_t(mask)};
            drawfx::Words(c.words, box, 6, "u");
            drawfx::gLog.push_back(std::move(c));
        } else {
            const char *const subset = kSubsets[rng() % std::size(kSubsets)];
            using Fn = const char *(__cdecl *)(cIGZUnknown *, const char *);
            const char *found;
            if (what == 1) found = original ? ctx.Original<Fn>(0x3acd30)(node, subset) : FindSubsetMaterial(node, subset);
            else found = original ? ctx.Original<Fn>(0x355470)(node, subset) : SubsetMaterial(node, subset);
            sgfx::NoteText(what == 1 ? "find material" : "subset material", found);
        }
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: ObjectBounds 0x3558d0, FindSubsetMaterial 0x3acd30, SubsetMaterial 0x355470, SwapLayerBitsDeep 0x354630
// and AddShadowMaterial 0x3546b0 called directly; cObjectShadowFilter's Accept 0x356a70 through its table, and its
// deleting destructor 0x354580 with the +4 table's thunk 0x3545a3 (ours: our +4 table's slot 3).
DIFF_TEST(sg_shadow_helpers, 0x3558d0)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "shadow helpers", 400, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunHelpers(ctx, original, rng);
            ++ctx.cases;
        });
}

namespace {

float SpanValue(std::mt19937 &rng)
{
    static const float kValues[] = {0.0f,  0.5f,  1.5f, 2.5f,       -0.5f, -1.5f, 0.49999997f, 0.50000006f, 1.2f, -2.7f,
                                    3.0f,  4194304.5f, 1e7f, -1e7f, kNan,  1e-3f, -0.0f,       0.25f,       2.0f, -3.0f};
    return kValues[rng() % std::size(kValues)];
}

void RunFloor(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    cIGZUnknown *const nodes[] = {reinterpret_cast<cIGZUnknown *>(&gw.objects[0]), reinterpret_cast<cIGZUnknown *>(&gw.objects[1]),
                                  reinterpret_cast<cIGZUnknown *>(&gw.kids[1]), reinterpret_cast<cIGZUnknown *>(&gw.kids[2])};
    static const char *const kSubsets[] = {"groundshadow", "wallshadow", "eastwallshadow", "other", ""};
    static const float kThin[] = {0.0f, 0.004f, 0.0099f, 0.01f, 0.0101f, -0.004f, 0.6f, 1.0f, 2.0f};
    const unsigned steps = 4 + rng() % 24;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        cIGZUnknown *const node = nodes[rng() % std::size(nodes)];
        const unsigned what = rng() % 5;
        if (what == 0) {
            const char *const subset = kSubsets[rng() % std::size(kSubsets)];
            float out[6] = {-7.0f, -7.0f, -7.0f, -7.0f, -7.0f, -7.0f};
            using Fn = void(__cdecl *)(cIGZUnknown *, const char *, float *);
            if (original) ctx.Original<Fn>(0xae7f00)(node, subset, out);
            else SubsetBounds(node, subset, out);
            drawfx::Call c;
            c.note = "subset bounds";
            drawfx::Words(c.words, out, 6, "u");
            drawfx::gLog.push_back(std::move(c));
        } else if (what == 1) {
            const char *const subset = kSubsets[rng() % std::size(kSubsets)];
            float box[6];
            const uint32_t r = rng() % 3;
            for (unsigned k = 0; k < 6; ++k)
                box[k] = r == 0 ? (k < 3 ? 3.4e38f : -3.4e38f) : r == 1 ? kNan : float(int32_t(rng() % 5) - 2);
            using Fn = void(__cdecl *)(float *, const char *, cIGZUnknown *);
            if (original) ctx.Original<Fn>(0x355990)(box, subset, node);
            else SubsetWorldBounds(box, subset, node);
            drawfx::Call c;
            c.note = "subset world bounds";
            drawfx::Words(c.words, box, 6, "u");
            drawfx::gLog.push_back(std::move(c));
        } else if (what == 2) {
            // A box near tile edges, often thin in x or y (or both), so the widening runs both ways.
            float box[6];
            for (float &f : box) f = SpanValue(rng);
            const uint32_t thin = rng() % 4;
            if (thin & 1) box[3] = box[0] + kThin[rng() % std::size(kThin)];
            if (thin & 2) box[4] = box[1] + kThin[rng() % std::size(kThin)];
            if (rng() % 3 == 0) {
                box[0] = SpanValue(rng) + 0.3f * float(rng() % 3);
                box[3] = box[0] + kThin[rng() % std::size(kThin)];
                box[1] = SpanValue(rng);
                box[4] = box[1] + kThin[rng() % std::size(kThin)];
            }
            int32_t span[4] = {0x1111, 0x2222, 0x3333, 0x4444};
            using Fn = void(__cdecl *)(const float *, int32_t *, int32_t *, int32_t *, int32_t *);
            if (original) ctx.Original<Fn>(0x355770)(box, &span[0], &span[1], &span[2], &span[3]);
            else TileSpan(box, &span[0], &span[1], &span[2], &span[3]);
            drawfx::Call c;
            c.note = "tile span";
            drawfx::Words(c.words, box, 6, "u");
            drawfx::Words(c.words, span, 4, "u");
            drawfx::gLog.push_back(std::move(c));
        } else {
            const uint32_t subset = rng() % cShadow::kSubsets;
            using Fn = uint8_t(__cdecl *)(cIGZUnknown *, uint32_t);
            const uint8_t r = original ? ctx.Original<Fn>(0x3554b0)(node, subset) : FloorTest(node, subset);
            drawfx::Note("floor test", {subset, uint32_t(r)});
        }
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: SubsetBounds 0xae7f00, SubsetWorldBounds 0x355990, TileSpan 0x355770 and FloorTest 0x3554b0 called
// directly (FloorTest also as ApplyMaterials reaches it in sg_shadow_life).
DIFF_TEST(sg_shadow_floor, 0x3554b0)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "shadow floor", 600, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunFloor(ctx, original, rng);
            ++ctx.cases;
        });
}
