// gmdc_decode: milestone N's standalone program (docs/roadmap.md, N; the lane list in docs/subsystems/scenegraph.md,
// "Milestone N"). Opens a .package through our packed-file segment (cGZDBSegmentPackedFile) and its DBPF index, finds
// a GMDC resource (type 0xac4f8687), reads it with our cGZPersistBinaryReader into our cGeometryDataContainer, and
// prints a summary: elements (identity, formats, sizes), groups, name pairs, bounds of the vertex element, and last the
// canonical summary line (`summary ...`, standalone/gmdc_summary.h) the acceptance difftest computes for the original.
//
//   gmdc_decode <file.package> [instance-hex [group-hex [resource-hex]]]   (the first GMDC whose key matches)
//
// Built from our sources with no game image: not tools/gamebase.asm, not the generated origsyms.asm, not the harness.
// Every GameAddress and OriginalFunction still reachable from here is an unresolved symbol at link time; the list of
// them is milestone N's work (the "Milestone N" section of docs/subsystems/scenegraph.md).
//
// The path is the game's: the segment's Init, SetPath, Open (read), its key list (sub slot 45 -> the index's slot 10)
// through a type filter, OpenRecord (sub slot 37, read) and the record's cIGZIStream face (0x051b11bd), then the
// reader's ReadRoot with the container's cISGResource face as the root (its slot 8 gives the cIGZPersistResource whose
// key ReadRoot registers; its slot 3 is Read). The reader is constructed (Construct) with its owned vtable; the
// framework (game global 0x10c9378), its resource manager service and the root COM director with the GMDC and
// triangle-set factories are stood up by StandUp, which the game's start-up (WinMain, the directors' OnStart) does.
#include "gmdc_summary.h"

#include "compgeom/TriangleSet.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceManagerShape.h"
#include "gz/cGZPersistBinaryReader.h"
#include "gz/cIGZDBPFIndex.h"
#include "gz/cIStreamStringBehaviourAdaptor.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cSceneGraphSystem.h"

#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

using nGZSceneGraph::cGeometryDataContainer;
using nGZSceneGraph::cIGeometryDataContainer;
using nGZSceneGraph::cISGResource;

constexpr uint32_t kGmdcType = 0xac4f8687;   // the GMDC resource type (also the container's class id)
constexpr uint32_t kTriangleSetClass = 0xfcfe5320; // nGZCompGeom::cITriangleSetShape, also the director's class id
constexpr uint32_t kIStreamIid = 0x051b11bd; // cIGZIStream, asked of an opened record
constexpr int kQueryChildSlot = 15;            // a database record's QueryChild (its child: the segment)
constexpr uint32_t kContextIid = 0x656b8f0c;  // what the scene-graph factory asks the record's child for, as ReadRoot's context
constexpr uint32_t kVertexIdentity = 0x5b830781; // the element that holds positions (three floats per vertex)
constexpr uint32_t kSceneGraphSystemIid = 0x25074b4d; // what the string pool's getter asks the system service for

// cIGZPersistResourceKeyFilter: slot 3 says whether a key goes into the list.
struct TypeFilter {
    virtual bool QueryInterface(uint32_t, void **) { return false; } // 0
    virtual uint32_t AddRef() { return 1; }                          // 1
    virtual uint32_t Release() { return 1; }                         // 2
    virtual bool IsKeyIncluded(const cGZPersistResourceKey *key)     // 3
    {
        return key->type == kGmdcType && (mFields < 1 || key->instance == mInstance) &&
               (mFields < 2 || key->group == mGroup) && (mFields < 3 || key->resource == mResource);
    }
    int mFields = 0; // how many of instance, group, resource must match
    uint32_t mInstance = 0, mGroup = 0, mResource = 0;
};

// The start-up the reader's service query and object creation need (what WinMain and the directors' OnStart do in the game).
// The real framework (gz::cCTGFramework: message server, command line, debug streams) is more than the path uses, and
// its vtable would root every slot, so a small framework answers the calls the path makes (a function table, as for
// the cGZ* vtables; slots nobody calls trap), stored in the globals the helpers read (g_Framework 0x10c9378, 0x10c937c):
//  - slot 5, GetSystemService: our persist resource manager (id 0x56b906e), which ReadRoot asks for, and a stand-in
//    scene-graph system (id 0xe5074b6b) whose slot 8 is the string pool (NamePool below) where the resource's Read
//    interns its name (ServiceCell, GetCachedObjectB);
//  - slot 23, GetCOMObject: the COM object the root director takes as its mCOM (GetCOMDirector's constructor), whose
//    slot 3, GetClassObject, is the director's;
//  - the root COM director (GetCOMDirector, the static at 0x10c9390) with the two classes the path creates: the GMDC
//    container (class 0xac4f8687, factory cGeometryDataContainer::Create) and the triangle set (class 0xfcfe5320,
//    factory cTriangleSetShape::Create 0x196560).
struct StandIn {
    void **vtable;
    void *manager; // the framework's only
};

void __fastcall Unexpected(void *, void *)
{
    std::fprintf(stderr, "gmdc_decode: an unexpected framework call\n");
    std::abort();
}

StandIn gCOM;
StandIn gSceneGraph;
void *gFrameworkTable[40];
void *gCOMTable[12];
void *gSceneGraphTable[12];

bool __fastcall FrameworkGetSystemService(StandIn *self, void *, uint32_t serviceId, uint32_t iid, void **ppv)
{
    if (!ppv) return false;
    if (serviceId == nGZSceneGraph::cSceneGraphSystem::kServiceId && iid == kSceneGraphSystemIid) {
        *ppv = &gSceneGraph;
        return true;
    }
    if (serviceId != gz::cGZPersistResourceManager::kServiceId) return false;
    return static_cast<gz::cGZPersistResourceManager *>(self->manager)->QueryInterface(iid, ppv);
}

// The string pool the resource's name goes into (cIStringPool slots 5-8). Our cStringPool (src/sg/cStringPool.cpp) still
// names two image addresses (GetText's empty text 0xd9b9d0, the handle iterator's fixed allocator 0x10f8c60), so this
// program keeps a small pool of its own: equal texts (lowercased unless the caller keeps case) share a handle.
struct NamePool {
    std::vector<std::string> texts;
    std::vector<int32_t> counts;
} gNames;

uint32_t __fastcall PoolIntern(StandIn *, void *, const char *text, bool caseSensitive)
{
    if (text == nullptr) return 0xffffffffu;
    std::string key(text);
    if (!caseSensitive)
        for (char &ch : key) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    for (size_t i = 0; i < gNames.texts.size(); ++i)
        if (gNames.texts[i] == key) return uint32_t(i);
    gNames.texts.push_back(key);
    gNames.counts.push_back(0);
    return uint32_t(gNames.texts.size() - 1);
}
void __fastcall PoolAddRef(StandIn *, void *, uint32_t handle)
{
    if (handle < gNames.counts.size()) ++gNames.counts[handle];
}
void __fastcall PoolRelease(StandIn *, void *, uint32_t handle)
{
    if (handle < gNames.counts.size()) --gNames.counts[handle];
}
const char *__fastcall PoolText(StandIn *, void *, uint32_t handle)
{
    return handle < gNames.texts.size() ? gNames.texts[handle].c_str() : "";
}

StandIn gPool;
void *gPoolTable[14];

uint32_t __fastcall StaticCount(StandIn *, void *) { return 1; } // static objects: AddRef, Release
void *__fastcall SceneGraphGetStringPool(StandIn *, void *) { return &gPool; }

void *__fastcall FrameworkGetCOMObject(StandIn *, void *) { return &gCOM; }

uint32_t __fastcall COMGetClassObject(StandIn *, void *, uint32_t classId, uint32_t iid, void **ppv)
{
    return reinterpret_cast<rz::cGZCOMDirector *>(GetCOMDirector())->GetClassObject(classId, iid, ppv);
}

void StandUp()
{
    static StandIn framework;
    for (void *&slot : gFrameworkTable) slot = reinterpret_cast<void *>(&Unexpected);
    for (void *&slot : gCOMTable) slot = reinterpret_cast<void *>(&Unexpected);
    gFrameworkTable[5] = reinterpret_cast<void *>(&FrameworkGetSystemService);
    gFrameworkTable[23] = reinterpret_cast<void *>(&FrameworkGetCOMObject);
    gCOMTable[3] = reinterpret_cast<void *>(&COMGetClassObject);
    gCOM.vtable = gCOMTable;
    for (void *&slot : gSceneGraphTable) slot = reinterpret_cast<void *>(&Unexpected);
    gSceneGraphTable[1] = gSceneGraphTable[2] = reinterpret_cast<void *>(&StaticCount);
    gSceneGraphTable[8] = reinterpret_cast<void *>(&SceneGraphGetStringPool);
    gSceneGraph.vtable = gSceneGraphTable;
    for (void *&slot : gPoolTable) slot = reinterpret_cast<void *>(&Unexpected);
    gPoolTable[1] = gPoolTable[2] = reinterpret_cast<void *>(&StaticCount);
    gPoolTable[5] = reinterpret_cast<void *>(&PoolIntern);
    gPoolTable[6] = reinterpret_cast<void *>(&PoolAddRef);
    gPoolTable[7] = reinterpret_cast<void *>(&PoolRelease);
    gPoolTable[8] = reinterpret_cast<void *>(&PoolText);
    gPool.vtable = gPoolTable;
    framework.vtable = gFrameworkTable;
    static gz::cGZPersistResourceManager manager;
    // The framework's reference, as the game's service registry holds one: every service query AddRefs the manager and
    // its caller releases it (ReadRoot's RZAutoRef), which without this would reach 0 and delete the static.
    manager.AddRef();
    framework.manager = &manager;
    ts2_rva_10c9378 = ts2_rva_10c937c = &framework;

    auto *director = reinterpret_cast<rz::cGZCOMDirector *>(GetCOMDirector());
    director->AddCls(kGmdcType, reinterpret_cast<void *>(&cGeometryDataContainer::Create));
    director->AddCls(kTriangleSetClass, reinterpret_cast<void *>(&nGZCompGeom::cTriangleSetShape::Create));
}

template <typename Fn, int Slot, typename... Args> auto Call(void *object, Args... args)
{
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[Slot])(object, args...);
}

void PrintSummary(const cGeometryDataContainer &c)
{
    std::printf("elements: %u\n", unsigned(c.mElements.size()));
    for (const auto &e : c.mElements) {
        std::printf("  [%u] identity 0x%08x number %u repetition %u block %u set %u: %u bytes, %u indices\n",
                    unsigned(e.mIndex), unsigned(e.mIdentity), unsigned(e.mNumber), unsigned(e.mRepetition),
                    unsigned(e.mBlockFormat), unsigned(e.mSetFormat), unsigned(e.mData.size()),
                    unsigned(e.mIndices.size()));
        if (e.mIdentity == kVertexIdentity && e.mData.size() >= 12) {
            float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
            const size_t count = e.mData.size() / 12;
            for (size_t i = 0; i < count; ++i) {
                float v[3];
                std::memcpy(v, e.mData.data() + i * 12, sizeof(v));
                for (int k = 0; k < 3; ++k) {
                    if (v[k] < lo[k]) lo[k] = v[k];
                    if (v[k] > hi[k]) hi[k] = v[k];
                }
            }
            std::printf("      %u vertices, bounds (%g %g %g) - (%g %g %g)\n", unsigned(count), lo[0], lo[1], lo[2],
                        hi[0], hi[1], hi[2]);
        }
    }
    std::printf("linkages: %u\n", unsigned(c.mLinkages.size()));
    std::printf("groups: %u\n", unsigned(c.mGroups.size()));
    for (const auto &g : c.mGroups)
        std::printf("  [%u] \"%s\" prim %u, %u indices, link %u, %u bones\n", unsigned(g.mIndex), g.mName.c_str(),
                    unsigned(g.mPrimType), unsigned(g.mVertexIndices.size()), unsigned(g.mLinkIndex),
                    unsigned(g.mBones.size()));
    std::printf("transforms: %u\n", unsigned(c.mTransforms.size()));
    std::printf("name pairs: %u\n", unsigned(c.mNamePairs.size()));
    for (const auto &p : c.mNamePairs) std::printf("  \"%s\" = \"%s\"\n", p.first.c_str(), p.second.c_str());
    std::printf("subset meshes: %u, bounding mesh: %s\n", unsigned(c.mSubsetMeshes.size()),
                c.mBoundingMesh ? "yes" : "no");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: gmdc_decode <file.package> [instance-hex [group-hex [resource-hex]]]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0); // what was printed survives a crash
    TypeFilter filter;
    uint32_t *const fields[] = {&filter.mInstance, &filter.mGroup, &filter.mResource};
    for (int i = 2; i < argc && i < 5; ++i) {
        *fields[i - 2] = uint32_t(std::strtoul(argv[i], nullptr, 16));
        filter.mFields = i - 1;
    }

    // The segment, as the game makes one (operator new 0x1b8, the constructor).
    auto *file = static_cast<cGZDBSegmentPackedFile *>(::operator new(sizeof(cGZDBSegmentPackedFile)));
    file->Construct();
    // Our reference (the game's resource manager holds one on every segment it opens): an open record holds the
    // segment as its child, and closing the record releases it, which without ours would delete the segment inside
    // DoCloseRecord, under its own pending-record map.
    Call<uint32_t(__thiscall *)(void *), 1>(file);
    auto *segment = reinterpret_cast<SegmentView *>(reinterpret_cast<uint8_t *>(file) + 4);

    alignas(cRZString) uint8_t pathStorage[sizeof(cRZString)];
    auto *path = reinterpret_cast<cRZString *>(pathStorage);
    path->Construct(argv[1]);
    if (!segment->Init() || !segment->SetPath(path) || !segment->Open(1, 0)) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }

    cGZDBPFResourceKeyList keys;
    segment->DoGetResourceKeyList(uint32_t(reinterpret_cast<uintptr_t>(&keys)),
                                  uint32_t(reinterpret_cast<uintptr_t>(&filter)));
    std::printf("%s: %u GMDC resources\n", argv[1], unsigned(keys.size()));
    if (keys.empty()) return 1;
    const cGZPersistResourceKey key = keys.front();
    std::printf("GMDC group 0x%08x instance 0x%08x resource 0x%08x\n", unsigned(key.group), unsigned(key.instance),
                unsigned(key.resource));

    void *record = nullptr;
    if (!segment->DoOpenRecord(&key.type, &record, 1) || record == nullptr) {
        std::fprintf(stderr, "cannot open the record\n");
        return 1;
    }
    void *stream = nullptr;
    if (!Call<bool(__thiscall *)(void *, uint32_t, void **), 0>(record, kIStreamIid, &stream) || stream == nullptr) {
        std::fprintf(stderr, "the record has no cIGZIStream face\n");
        return 1;
    }

    StandUp();
    alignas(cGZPersistBinaryReader) uint8_t readerStorage[sizeof(cGZPersistBinaryReader)];
    auto &reader = *reinterpret_cast<cGZPersistBinaryReader *>(readerStorage);
    reader.Construct();
    reader.mRefCount = 1;

    cISGResource *resource = cGeometryDataContainer::Create();
    void *face = nullptr;
    if (!Call<bool(__thiscall *)(void *, uint32_t, void **), 0>(resource, cIGeometryDataContainer::kIid, &face)) {
        std::fprintf(stderr, "the container has no cIGeometryDataContainer face\n");
        return 1;
    }
    // The root is the cISGResource face (a cIGZPersistResource2): ReadRoot takes the key from its slot 8's
    // cIGZPersistResource (slot 3) and reads it through its slot 3. The cIGeometryDataContainer face would not do: its
    // slot 8 is the cISGResource itself, whose slot 3 is Read.
    //
    // The stream is the scene-graph factory's (nGZSceneGraph::cSceneGraphFactory::Read, 0xa4d750): the record's
    // cIGZIStream wrapped in a cIStreamStringBehaviourAdaptor, so that names are read as the writer wrote them (a packed
    // length and the characters) and not by the record's own GetGZStr, which misreads a name of 32 or more characters.
    // The context is what the factory passes too: the record's child (its segment) queried through the record's slot 15.
    auto *adaptor = static_cast<cIStreamStringBehaviourAdaptor *>(::operator new(sizeof(cIStreamStringBehaviourAdaptor)));
    adaptor->Construct(stream);
    Call<uint32_t(__thiscall *)(void *), 1>(adaptor);
    void *context = nullptr;
    Call<bool(__thiscall *)(void *, uint32_t, void **), kQueryChildSlot>(record, kContextIid, &context);
    reader.ReadRoot(resource, adaptor, context);
    if (context != nullptr) Call<uint32_t(__thiscall *)(void *), 2>(context);
    Call<uint32_t(__thiscall *)(void *), 2>(adaptor);
    const auto &container = *static_cast<cGeometryDataContainer *>(static_cast<cIGeometryDataContainer *>(face));
    PrintSummary(container);
    std::printf("summary %s\n", gmdcsum::Summarize(container).Line().c_str());

    reader.DeletingDestructor(0);
    Call<uint32_t(__thiscall *)(void *), 2>(stream);
    segment->DoCloseRecord(record);
    segment->Close();
    Call<uint32_t(__thiscall *)(void *), 2>(file); // our reference: the last one, so the segment deletes itself
    return 0;
}
