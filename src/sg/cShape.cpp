// nGZSceneGraph::cShape (lane RY, scenegraph H1a). Layout, faces and the switch: see the header.
#include "sg/SGEventRecords.h"
#include "sg/cShape.h"

#include "app/AppServices2.h" // AllocateViaAllocatorService, FreeViaAllocatorService: slot 56 grows the caller's vector
#include "compgeom/CompGeom.h"
#include "compgeom/ConvexShapes.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cEventFaces.h" // rzface::cNullEvent
#include "gz/cIGZPersistResourceManager.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject: the material manager
#include "rz/RZAutoRef.h"
#include "rz/cRZStringSub.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/GameAllocator.h" // GZServiceAllocator
#include "rz/RZMemory.h"
#include "rz/StaticGetters2.h" // Dispatch8
#include "rz/StringKeyParser.h"
#include "sg/SGLocalString.h"
#include "sg/SGShapeGeometry.h"          // slots 49 / 50 / 58
#include "gz/cGZAny.h"                  // AnyUint32
#include "gz/cIGZPropertySet.h"         // GZServiceString
#include "gz/cInputDeviceMap.h"          // nGZEvent::cIEventBody
#include "sg/cGeometryNode.h"            // cIGeometryNode
#include "sg/cGeometryNodeIterators.h"  // cIGeometrySubsetIterator
#include "sg/cTextureManager.h"         // ResolveResourceName
#include "sg/cStringPool.h"              // cIStringPool
#include "sg/cMaterialManager.h"         // cIMaterialManager
#include "sg/cLinearFogLight.h"          // cSimpleLightBehaviorTraits
#include "sg/cObjectGraphNodeImpl.h"     // NewTypeName, cObjectGraphBehaviorTraits
#include "sg/cRenderGroupNameIterator.h" // RenderGroupRegistry
#include "sg/cSubsetBindingIterator.h"   // slot 54

#include "game/game.h"
#include "gz/cGZEventReflector.h" // cAbstractBehaviorType::RegisterBaseEvents

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

extern "C" __declspec(thread) int _Init_thread_epoch; // the CRT's thread-safe statics, on the game's guard (as app/AppDebugger.cpp)

namespace nGZSceneGraph {

static_assert(sizeof(cShape) == cShape::kSize, "cShape size");
static_assert(offsetof(cShape, mParts) == 0x4c, "cShape parts");
static_assert(offsetof(cShape, mBindings) == 0x58, "cShape bindings");
static_assert(offsetof(cShape, mFirstPartInfo) == 0x60, "cShape first-part words");
static_assert(offsetof(cShape, mLods) == 0x74, "cShape levels");
static_assert(offsetof(cShape, mPartNames) == 0x88, "cShape part names");
static_assert(sizeof(std::deque<int32_t>) == 0x14, "the deque is the original's: proxy, map, size, offset, count");

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

cIMaterialManager *MaterialManager() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

// A part name's cell: the string pool's cell for the text, empty (0xffffffff) for a null or empty text (0xaed300).
__forceinline void OpenNameCell(ServiceCell &cell, const char *name)
{
    if (name && *name)
        cell.Open(Word(name), 0);
    else
        cell.mValue = 0xffffffff;
}

// A part name appended as Read does it: the (cell, level) pair made from the opened cell, and push_back's own copy of
// the pair, each counting the handle again (the cells close as they go, the opened one last).
void PushPartName(std::vector<ShapePartName> &names, const OwnedCell &name, int32_t lod)
{
    const ShapePartName entry{name, lod};
    const ShapePartName pushed = entry;
    names.push_back(pushed);
}

// 0xae7340 (ours, sg/cTextureHandle.cpp): the text of `out` for a "##" text or a group-prefixed name.
__forceinline void ResolveGroupText(cRZString *out, const char *text, uint32_t group)
{
    ResolveResourceName(out->AsInterface(), text, group, nullptr);
}

// The reader's slot 28 (the group of the stream being read; the generated interface stops at slot 26).
__forceinline uint32_t ReaderGroup(ifaces::cIGZPersistReader *reader)
{
    using Fn = uint32_t(__thiscall *)(void *);
    return (*reinterpret_cast<Fn **>(reader))[28](reader);
}

// nGZSceneGraph::cMaterialBindingsUpdated (vtable 0xe6f1bc, 8 bytes): what slot 34 tells the listeners (id 0x3d44b140).
// Slots 1 and 2 copy the flag byte (0xa5cb90), 3 is the type, 4 and 5 are the folded 8 and 4 (0x1559c0, 0x155a00), 6
// builds a copy in caller memory (0xa5d1c0).
class MaterialBindingsUpdated final : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kEventId = 0x3d44b140;
    static constexpr uint32_t kType = 0x2c27f3de;
    ~MaterialBindingsUpdated() override {}
    cIEventBody *Assign(const cIEventBody *other) override
    {
        mFlag = static_cast<const MaterialBindingsUpdated *>(other)->mFlag;
        return this;
    }
    cIEventBody *Slot2(const cIEventBody *other) override { return Assign(other); }
    uint32_t GetEventType() const override { return kType; }
    uint32_t GetSize() const override { return 8; }
    uint32_t GetBodySize() const override { return 4; }
    cIEventBody *CloneInto(void *memory) const override
    {
        MaterialBindingsUpdated *copy = new (memory) MaterialBindingsUpdated;
        copy->mFlag = mFlag;
        return copy;
    }
    uint8_t mFlag = 0; // +4
};

// A name's cell counted from its handle (0xaeb710: the string pool's slot 6).
OwnedCell CountedCell(uint32_t handle)
{
    OwnedCell cell(handle);
    static_cast<cIStringPool *>(GetCachedObjectB())->AddRefHandle(handle);
    return cell;
}

// 0xa5f710 (ret 8, this the shape): the subset's material is the named one (found or loaded, counted), or none for no
// name.
void BindSubsetMaterial(cShape *shape, const char *material, const char *subset)
{
    OwnedCell key;
    OpenNameCell(key.cell, subset);
    if (material && *material) {
        const ShapeMaterialHandle found(static_cast<int32_t>(MaterialManager()->FindOrLoad(material)));
        shape->mBindings[key].mMaterial.Assign(found);
    } else {
        shape->mBindings[key].mMaterial.mIndex = -1; // (stored raw: the old one is not dropped)
    }
}

// 0xa5fbb0 (this the shape at +0): every part's materials and groups again from the bindings, and the first part's words.
void BindPartMaterials(cShape *shape)
{
    static const char *const kLodLower = "lod"; // 0xe6f738
    static const char *const kLodUpper = "LOD"; // 0xe6f73c
    for (ShapePart &part : shape->mParts) {
        part.mMaterials.clear();
        part.mGroups.clear();
        cIGeometryNode *const node = reinterpret_cast<cIGeometryNode *>(part.mObject);
        const uint32_t count = *node->GetRenderConst()->GetFlags();
        part.mMaterials.resize(count, ShapeMaterialHandle(-1));
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t handle = node->GetElementNameHandle(i);
            auto found = [&] {
                const OwnedCell cell = CountedCell(handle);
                return shape->mBindings.find(cell);
            };
            auto it = found();
            const char *const name = reinterpret_cast<const char *>(static_cast<uintptr_t>(node->GetElementName(i)));
            if (it == shape->mBindings.end() && name && *name && (strstr(name, kLodLower) || strstr(name, kLodUpper))) {
                // A level-of-detail subset nobody bound gets its own name as its material.
                BindSubsetMaterial(shape, name, name);
                it = found();
            }
            if (it == shape->mBindings.end()) continue;
            part.mMaterials[i].Assign(it->second.mMaterial);
            if (it->second.mMask != 0 || it->second.mWord != 0) {
                if (part.mGroups.empty()) part.mGroups.resize(count);
                part.mGroups[i] = ShapeSubsetGroups{it->second.mMask, it->second.mWord};
            }
        }
    }
    std::fill(std::begin(shape->mFirstPartInfo), std::end(shape->mFirstPartInfo), 0u);
    if (!shape->mParts.empty()) {
        cIGeometryNode *const node = reinterpret_cast<cIGeometryNode *>(shape->mParts.front().mObject);
        const uint32_t *const record = node->GetRenderConst()->GetFlags();
        shape->mFirstPartInfo[0] = record[1];
        shape->mFirstPartInfo[1] = record[2];
        shape->mFirstPartInfo[2] = record[0];
        shape->mFirstPartInfo[3] = record[3];
        shape->mFirstPartInfo[4] = record[4];
    }
}

// 0xa5b600 -> 0xa59f00: the parts by level. Up to 32 parts the game's insertion sort (each part after the first is moved out,
// goes before the first or is shifted in, and moves back: the counted moves its test logs); more parts take the library's
// sort, whose moves differ (the order of the result is the same: levels are unique).
void SortParts(std::vector<ShapePart> &parts)
{
    if (parts.size() > 32) {
        std::sort(parts.begin(), parts.end(), [](const ShapePart &a, const ShapePart &b) { return a.mLod < b.mLod; });
        return;
    }
    ShapePart *const first = parts.data();
    ShapePart *const last = first + parts.size();
    for (ShapePart *i = first + 1; i < last; ++i) {
        ShapePart value(std::move(*i));
        ShapePart *slot = i;
        if (value.mLod < first->mLod) {
            for (; slot != first; --slot) *slot = std::move(*(slot - 1));
        } else {
            for (; value.mLod < (slot - 1)->mLod; --slot) *slot = std::move(*(slot - 1));
        }
        *slot = std::move(value);
    }
}

// The framework's slot 5 (+0x14): the service (id, interface id) into `held`.
void GetFrameworkService(uint32_t service, uint32_t iid, RZAutoRef &held)
{
    void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378())); // receiver: cGZFramework
    if (framework != nullptr)
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, service, iid, &held.mObject);
}

// 0xa5e280 (this unused): the preferred level of detail, from the preferences service's "systemSpecifiedPreferredLOD";
// without the property, the fallback object's (a service's, cached in 0x10f6310 under the guard 0x10f6314) own value, zero
// when it has none.
uint32_t PreferredLod()
{
    RZAutoRef prefs{nullptr};
    GetFrameworkService(0xac1887eu, 0x4bb0d269u, prefs);
    gz::AnyUint32 value(0xffffffffu);
    {
        GZServiceString key("systemSpecifiedPreferredLOD");
        // (A missing service crashes the original here, and ours.)
        static_cast<cIGZPropertySetString *>(prefs.mObject)->GetPropertyValue(&key, &value);
    }
    uint32_t level = value.mValue;
    if (level == 0xffffffffu) {
        int *guard = &ts2_rva_10f6314;
        if (*guard > _Init_thread_epoch) {
            _Init_thread_header(guard);
            if (*guard == -1) {
                try {
                    RZAutoRef holder{nullptr};
                    GetFrameworkService(0xe5074b6bu, 0x25074b4du, holder);
                    using Get = void *(__thiscall *)(void *);
                    ts2_rva_10f6310 = VirtualCall<Get, 12>(holder.mObject);
                    holder.ReleaseObject();
                } catch (...) {
                    _Init_thread_abort(guard);
                    throw;
                }
                _Init_thread_footer(guard);
            }
        }
        level = 0;
        void *const fallback = ts2_rva_10f6310;
        if (fallback != nullptr && VirtualCall<bool(__thiscall *)(void *), 10>(fallback)) {
            level = VirtualCall<uint32_t(__thiscall *)(void *), 11>(fallback);
            if (level == 0xffffffffu) level = 0;
        }
    }
    prefs.ReleaseObject();
    return level;
}

// 0xa5e470 (cdecl; the level is unused): the part object's name, asked and dropped (the resource's slot 17).
void TouchPartName(int32_t, cIGZUnknown *object)
{
    if (!object) return;
    cISGResource *resource = nullptr;
    if (object->QueryInterface(cISGResource::kIid, reinterpret_cast<void **>(&resource))) resource->GetName();
    if (resource) resource->Release();
}

// 0xa5dfa0 (cdecl; the cell by value, closed here): the resource of a part name, by the name as it stands or as the
// default group resolves it. True when the resource manager answers for either key (the object in *out, counted).
bool LoadNamedPart(OwnedCell cell, void **out)
{
    const char *const text = reinterpret_cast<const char *>(static_cast<uintptr_t>(Dispatch8(&cell.cell)));
    if (!text) return false;
    nTSSG::SGLocalString plain(text);
    nTSSG::SGLocalString resolved;
    ResolveGroupText(&resolved.value, plain.Text(), 0);
    StringKeyParser::Result first{};
    StringKeyParser::Result second{};
    StringKeyParser::ParseKey(&first, plain.Text(), cShape::kKeyType, nullptr);
    StringKeyParser::ParseKey(&second, resolved.Text(), cShape::kKeyType, nullptr);
    RZAutoRef manager{nullptr};
    GetFrameworkService(0x56b906eu, 0x656b8efcu, manager);
    auto *const resources = static_cast<cIGZPersistResourceManager *>(manager.mObject);
    bool found = (resources->vf3(Word(&first), cShape::kPartIid, Word(out), 0, 0) & 0xff) != 0;
    if (!found) found = (resources->vf3(Word(&second), cShape::kPartIid, Word(out), 0, 0) & 0xff) != 0;
    manager.ReleaseObject();
    return found;
}

// 0xa5de40 (this the shape): the level to keep: the last named part at or before `level` (by level) whose part is there or
// loads, added as it loads; -1 for none. The binary search makes a counted cell of nothing for each probe.
int32_t ChooseNamedLod(cShape *shape, int32_t level)
{
    auto &names = shape->mPartNames;
    if (names.empty()) return -1;
    size_t first = 0;
    size_t count = names.size();
    while (count > 0) {
        const size_t half = count >> 1;
        int32_t probed;
        {
            const OwnedCell scratch = CountedCell(0);
            probed = names[first + half].mLod;
        }
        if (probed < level) {
            first += half + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    if (first == names.size() || (names[first].mLod != level && first != 0)) --first;
    for (;;) {
        const int32_t lod = names[first].mLod;
        if (shape->PartObjectAtLod2(static_cast<uint32_t>(lod)) != 0) return lod;
        cIGZUnknown *object = nullptr;
        bool done = false;
        int32_t chosen = -1;
        if (LoadNamedPart(names[first].mName, reinterpret_cast<void **>(&object))) {
            shape->Face()->AddPart(object, lod);
            chosen = lod;
            done = true;
        } else if (first == 0) {
            done = true;
        } else {
            --first;
        }
        if (object) object->Release();
        if (done) return chosen;
    }
}

// 0xa5fa10 (this the shape): only the part of the level stays, the others are removed one by one; the part's bounds take
// the first one's when it has none.
void KeepLodPart(cShape *shape, int32_t level)
{
    if (shape->mParts.size() <= 1) return;
    const auto it = shape->FindPartRange(level);
    if (it != shape->mParts.begin()) {
        // ORIGINAL-BUG: the level may have no part (the end is found): the object is then read from the word after the last
        // part, which is not a part.
        const ShapePart *const found = &*it;
        auto *const firstNode = static_cast<cIGeometryNode *>(shape->mParts.front().mObject);
        auto *const foundNode = static_cast<cIGeometryNode *>(found->mObject);
        const uint32_t *const firstInfo = static_cast<const uint32_t *>(firstNode->GetBoundsInfo());
        const uint32_t *const info = static_cast<const uint32_t *>(foundNode->GetBoundsInfo());
        if (info[2] == 0 && firstInfo[2] != 0) foundNode->SetBounds(firstInfo[2], reinterpret_cast<cIGZUnknown *const *>(uintptr_t(firstInfo[3])));
    }
    while (shape->mParts.size() > 1) {
        const ShapePart &first = shape->mParts[0];
        const ShapePart &drop = first.mLod == level ? shape->mParts[1] : first;
        const int32_t lod = drop.mLod;
        TouchPartName(lod, drop.mObject);
        shape->Face()->RemovePart(lod);
    }
}

// Slot 44's chain, which Read calls directly.
void UpdateLodChain(cShape *shape)
{
    const uint32_t first = PreferredLod();
    const int32_t second = ChooseNamedLod(shape, static_cast<int32_t>(first));
    KeepLodPart(shape, second);
}

// 0xaed160 (cdecl): the name of the part at a level: the name itself for none, else "<before>LOD<n><after>" with the name
// split at its first "_" (without one, the whole name comes first).
void FormatLodName(const char *name, int32_t lod, cRZString *out)
{
    if (lod == 0) {
        out->mString.assign(name);
        return;
    }
    nTSSG::SGLocalString source(name);
    const uint32_t at = static_cast<uint32_t>(source.value.Find("_", 0, true));
    nTSSG::SGLocalString before;
    nTSSG::SGLocalString after;
    Sub(source.value).Left(&before.value, at);
    Sub(source.value).Mid(&after.value, at, 0xffffffffu);
    out->Sprintf("%sLOD%d%s", before.Text(), lod, after.Text());
}

// 0xa5c840 (ret 8): what Read does with a stream of a single part (its name, the reader's group): for each listed level
// of detail from 1 to 30, the part of that level's name (the name with "LOD<n>" before the extension), resolved in the group.
void ApplySinglePartName(cShape *shape, const char *name, uint32_t group)
{
    for (size_t i = 0; i < shape->mLods.size(); ++i) {
        const int32_t lod = shape->mLods[i];
        if (lod == 0 || lod >= 0x1f) continue;
        nTSSG::SGLocalString named;
        FormatLodName(name, lod, &named.value);
        nTSSG::SGLocalString resolved;
        ResolveGroupText(&resolved.value, named.Text(), group);
        OwnedCell cell;
        OpenNameCell(cell.cell, resolved.Text());
        PushPartName(shape->mPartNames, cell, lod);
    }
}

// Write's view of a material's name: after the "!" of a "*...!name" reference (0xda33f8 "*", 0xdbb414 "!").
std::string WrittenMaterialName(const char *name)
{
    std::string text = name;
    if (!text.empty() && text[0] == '*') {
        const size_t bang = text.find('!');
        if (bang != std::string::npos) text = text.substr(bang + 1);
    }
    return text;
}

uint32_t PopCount(uint32_t v)
{
    uint32_t count = 0;
    for (; v != 0; v &= v - 1) ++count;
    return count;
}

} // namespace

// ---- the behaviour template's registry (0xa5f010, with its holder 0xa59480 and the parent's map 0xa480c0) ----------------

namespace {

std::optional<nGZEvent::cEventAttributeMap> gShapeRegistry;

// 0x3a5d00: the map of the simple lights (and the shape's parent behaviour), a non-zero `id` storing *attribute in it
// first. Also reached with id 0 through 0xa480c0 for the map to copy.
nGZEvent::cEventAttributeMap *SimpleRegistry(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    return cSimpleLightBehaviorTraits::RegistryFor(id, attribute);
}

// 0x264010: the object-graph nodes' registry (as cGeometryNodeClass.cpp's).
nGZEvent::cEventAttributeMap *ObjectGraphRegistry(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    return cObjectGraphBehaviorTraits::RegistryFor(id, attribute);
}

// An event's record as the game's data holds it (24 bytes, owned in sg/SGEventRecords.h).
nGZEvent::cEventAttribute GameRecord(const nGZEvent::cEventAttribute &record) { return record; }

// The 20-byte records: the last word is the holder's own.
nGZEvent::cEventAttribute GameRecord20(const nGZEvent::cEventAttribute &record, uint32_t lastWord)
{
    nGZEvent::cEventAttribute attribute = GameRecord(record);
    attribute.mWord14 = lastWord;
    return attribute;
}

} // namespace

nGZEvent::cEventAttributeMap *ShapeEventRegistry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cShapeBehaviorTraits::RegistryFor(0, &defaults);
}

// Built on first use. The original runs its holder 0xa59480 first (on every call, a once byte skipping it after the
// first): the base events and the object-graph node's two (0x2570a0), the parent's map given the event 0xdd041f30, then
// this map, made as a copy of the parent's map (0xa480c0) holding the four events 0x9bb24282, 0x2a7e3a48, 0x3d44b140 and
// 0xedf79966. (The holder's 9-byte object, kept in the static 0x10f632c, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cShapeBehaviorTraits::RegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    if (!gShapeRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        ObjectGraphRegistry(0x7b54c99c, &sgevents::kGraphConnChanged);
        ObjectGraphRegistry(0x1d05597b, &sgevents::kSubordinateListChanged);
        const nGZEvent::cEventAttribute parentRecord = GameRecord(sgevents::kBoundsUpdated);
        SimpleRegistry(0xdd041f30, &parentRecord);
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gShapeRegistry.emplace(*SimpleRegistry(0, &defaults));
        nGZEvent::cEventAttributeMap &map = *gShapeRegistry;
        map[0x9bb24282] = GameRecord20(sgevents::kGeometryNodeUpdated, 4);
        map[0x2a7e3a48] = GameRecord20(sgevents::kHandleGeometryNodeUpdated, 0xc);
        map[0x3d44b140] = GameRecord(sgevents::kMaterialBindingsUpdated);
        map[0xedf79966] = GameRecord(sgevents::kPartCountUpdated);
    }
    if (id != 0) (*gShapeRegistry)[id] = *attribute;
    return &*gShapeRegistry;
}

void cShapeBehaviorTraits::ResetRegistryForTest() { gShapeRegistry.reset(); }

const char *ShapeObjectName(cIGZUnknown *object)
{
    if (!object) return nullptr;
    cISGResource *resource = nullptr;
    const char *name = nullptr;
    if (object->QueryInterface(cISGResource::kIid, reinterpret_cast<void **>(&resource))) {
        name = resource->GetName();
        if (name && !*name) name = nullptr;
    }
    if (resource) resource->Release();
    return name;
}

// The constructor's call of 0xa5fbb0 on the empty shape only zeroes the five first-part words, which the member
// initialiser does.
cShape::cShape() = default;

// The parts go through slot 42 one by one (the first part's level each time), then the members and bases as C++ does.
cShape::~cShape()
{
    while (!mParts.empty()) Face()->RemovePart(mParts.front().mLod);
}

// (The original's operator new throws on failure, so its null branch is dead.)
cIShape *__cdecl cShape::Create() { return new cShape; }

bool cShape::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIShape::kIid || iid == cIReferentNode::kIid) {
        *out = Face();
        AddRef();
        return true;
    }
    if (Node::QueryInterface(iid, out)) return true;
    return cSGResource::QueryInterface(iid, out);
}

// 0xa5d710: the behaviour's, then the two events this shape answers: 0x1d05597b tells the listeners three things in
// order (a null event 0xdd041f30, the materials updated 0x3d44b140 with the flag set, a null event 0xedf79966), and
// 0x9bb24282 binds every part's materials again.
void cShape::OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    Node::OnEvent(type, arg1, data, sender);
    if (type == 0x1d05597b) {
        rzface::cNullEvent first;
        NotifyListeners(0xdd041f30, 0, &first);
        MaterialBindingsUpdated updated;
        updated.mFlag = 1;
        NotifyListeners(MaterialBindingsUpdated::kEventId, 0, &updated);
        rzface::cNullEvent last;
        NotifyListeners(0xedf79966, 0, &last);
    } else if (type == 0x9bb24282) {
        BindPartMaterials(this);
    }
}

void cShape::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cShape *const clone = new cShape;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

void cShape::CopyContentTo(cShape *target, uint32_t mode)
{
    mImpl.CopyTo(this, target, mode, nullptr);
    if (&target->mBindings != &mBindings) target->mBindings = mBindings;
    for (const ShapePart &part : mParts) target->Face()->AddPart(part.mObject, part.mLod);
    if (&target->mPartNames != &mPartNames) target->mPartNames = mPartNames;
    BindPartMaterials(target);
}

void cShape::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    const uint32_t version = info.mVersion;
    cSGResource::Read(reader);
    if (version < 5)
        mImpl.cObjectGraphNodeImpl::Read(this, reader); // before version 5 the stream has no referent record
    else
        mImpl.Read(this, reader);

    if (version > 6) {
        const uint32_t count = reader->GetUint32();
        mLods.resize(count, 0);
        for (uint32_t i = 0; i < count; ++i) mLods[i] = reader->GetSint32();
    }
    if (mLods.empty()) mLods.push_back(kDefaultLod);

    uint32_t count = reader->GetUint32();
    std::string firstName; // the name of the (last) level-0 part
    if (count != 0) {
        uint32_t i = 0;
        do {
            const int32_t lod = version > 5 ? static_cast<int32_t>(reader->GetUint32()) : 0;
            if (version < 8 || !reader->GetBool()) {
                // A part by object reference.
                cIGZUnknown *object = nullptr;
                reader->GetObjectRef(kPartIid, &object);
                if (!object) {
                    // ORIGINAL-BUG: a missing reference shortens the list from the second one on (both the index and
                    // the count go back one), so the next entry is read in its place and the last one is not read.
                    if (i != 0) {
                        --i;
                        --count;
                    }
                } else {
                    const char *name = ShapeObjectName(object);
                    if (name) {
                        OwnedCell cell;
                        OpenNameCell(cell.cell, name);
                        PushPartName(mPartNames, cell, lod);
                        if (lod == 0) firstName = name;
                    }
                    Face()->AddPart(object, lod);
                }
                if (object) object->Release();
            } else {
                // A part by name (version 8 on), resolved in the reader's group.
                nTSSG::SGLocalString text;
                reader->GetGZStr(&text.value);
                const uint32_t group = ReaderGroup(reader);
                nTSSG::SGLocalString resolved;
                ResolveGroupText(&resolved.value, text.Text(), group);
                OwnedCell cell;
                OpenNameCell(cell.cell, resolved.Text());
                PushPartName(mPartNames, cell, lod);
                if (lod == 0) firstName = resolved.value.mString;
            }
            ++i;
        } while (i < count);
        if (count == 1) {
            const uint32_t group = ReaderGroup(reader);
            ApplySinglePartName(this, firstName.c_str(), group);
        }
    }
    std::sort(mPartNames.begin(), mPartNames.end(),
              [](const ShapePartName &a, const ShapePartName &b) { return a.mLod < b.mLod; });
    UpdateLodChain(this);

    const int32_t bindings = static_cast<int32_t>(reader->GetUint32());
    for (int32_t i = 0; i < bindings; ++i) {
        nTSSG::SGLocalString subset;
        nTSSG::SGLocalString material;
        reader->GetGZStr(&subset.value);
        reader->GetGZStr(&material.value);
        if (!material.value.mString.empty()) {
            const uint32_t group = ReaderGroup(reader);
            if (group == 0) {
                Face()->BindMaterial(material.Text(), subset.Text());
            } else {
                nTSSG::SGLocalString resolved;
                ResolveGroupText(&resolved.value, material.Text(), group);
                Face()->BindMaterial(resolved.Text(), subset.Text());
            }
        }
        const bool allGroups = reader->GetBool();
        const uint32_t groups = reader->GetUint32();
        for (uint32_t g = 0; g < groups; ++g) {
            nTSSG::SGLocalString group;
            reader->GetGZStr(&group.value);
            Face()->AddRenderGroup(group.Text(), subset.Text());
        }
        if (allGroups) Face()->SetRenderGroupMask(0xffffffffu, subset.Text());
        const uint32_t word = reader->GetUint32();
        if (word != 0) Face()->SetSubsetWord(word, subset.Text());
    }
    BindPartMaterials(this);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cShape::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cShape");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

void cShape::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cShape::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cSGResource::WriteResource(writer);
    mImpl.Write(this, writer);

    writer->SetUint32(static_cast<uint32_t>(mLods.size()));
    for (size_t i = 0; i < mLods.size(); ++i) writer->SetSint32(mLods[i]);

    // The resource manager, held only for the lookup (the original drops its reference at once and uses the pointer).
    RZAutoRef held{nullptr};
    void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378())); // receiver: cGZFramework
    if (framework != nullptr)
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, 0x56b906eu, 0x656b8efcu, &held.mObject);
    cIGZPersistResourceManager *const manager = static_cast<cIGZPersistResourceManager *>(held.mObject);
    held.ReleaseObject();

    writer->SetUint32(static_cast<uint32_t>(mParts.size()));
    for (size_t i = 0; i < mParts.size(); ++i) {
        writer->SetUint32(static_cast<uint32_t>(mParts[i].mLod));
        const char *name = ShapeObjectName(mParts[i].mObject);
        writer->SetUint8(name != nullptr);
        if (!name) {
            writer->WriteObjectRef(mParts[i].mObject);
            continue;
        }
        // A named part is written by name, and its resource is made known to the resource manager under that key.
        writer->WriteRZCharStr(const_cast<char *>(name));
        StringKeyParser::Result key{};
        StringKeyParser::ParseKey(&key, std::string(name).c_str(), kKeyType, nullptr);
        cIGZUnknown *const object = mParts[i].mObject;
        cISGResource *resource = nullptr;
        if (object && object->QueryInterface(cISGResource::kIid, reinterpret_cast<void **>(&resource))) {
            cIGZUnknown *const segment = resource->GetSegment();
            cIGZPersistResource *const persist = resource->GetPersistResource();
            manager->vf14(persist, &key, segment);
        }
        if (resource) resource->Release();
    }

    cIMaterialManager *const materials = MaterialManager();
    writer->SetUint32(static_cast<uint32_t>(mBindings.size()));
    for (const auto &binding : mBindings) {
        writer->WriteRZCharStr(
            reinterpret_cast<char *>(static_cast<uintptr_t>(Dispatch8(const_cast<ServiceCell *>(&binding.first.cell)))));
        // The info's first word is the material's name text.
        const MaterialInfo material = materials->GetInfo(binding.second.mMaterial.mIndex);
        const char *materialName = reinterpret_cast<const char *>(static_cast<uintptr_t>(material.nameHash));
        std::string text = WrittenMaterialName(materialName ? materialName : "");
        writer->WriteRZCharStr(text.data());
        const uint32_t mask = binding.second.mMask;
        writer->SetUint8(mask == 0xffffffffu);
        const uint32_t groups = PopCount(mask);
        writer->SetUint32(groups);
        // ORIGINAL-BUG: the names written are those of bits 0 .. count-1, not of the mask's set bits.
        for (int32_t bit = 0; bit < static_cast<int32_t>(groups); ++bit)
            writer->WriteRZCharStr(const_cast<char *>(RenderGroupRegistry()->NameOfBit(static_cast<uint32_t>(bit))));
        writer->SetUint32(binding.second.mWord);
    }
}


// ---- cIShape (every slot ours since H1b part 6) ----

// 0xaed300: the name cell the slots below find their subset by.
ServiceCell *__cdecl MakeNameCell(ServiceCell *out, const char *name)
{
    OpenNameCell(*out, name);
    return out;
}

// A draw element as the render's slot 5 hands it out: the format object at +0xc (its slot 3 the format's index), a
// flag byte at +0x34.
namespace {
class cIElementFormat : public cIGZUnknown {
public:
    virtual uint32_t FormatIndex() = 0; // 3
};
// A material variant as the variant iterators' Get writes it: three key words, then the variant object.
struct VariantEntry {
    uint32_t key[3];
    cIGZUnknown *variant;
};
// The variant iterators (cMaterialManager's range of slot 35).
class cIVariantIterator : public cIGZUnknown {
public:
    virtual cIVariantIterator *Clone() = 0;                // 3
    virtual bool IsEqual(cIGZUnknown *other) = 0;          // 4
    virtual VariantEntry *Get(VariantEntry *out) = 0;      // 5 (ret 4)
    virtual void Next() = 0;                               // 6
};
// The variant object's slot 9: a record whose first word says whether it is in use and whose byte +0xc is a format.
using VariantRecordFn = const uint32_t *(__thiscall *)(cIGZUnknown *);
} // namespace

void __cdecl RequestDrawVariants(cIGeometryRender *render, uint32_t index, int32_t material)
{
    if (material == -1) return;
    const uint8_t *const element = reinterpret_cast<const uint8_t *>(render->GetDrawInfo(index));
    auto *const format = *reinterpret_cast<cIElementFormat *const *>(element + 0xc);
    uint32_t key[3] = {0, 0, 0};
    key[2] = (format->FormatIndex() & 0x7f) << 4 | (element[0x34] != 0 ? 1u : 0u);
    MaterialManager()->GetVariant(uint32_t(material), key, 0, 0, 0);
    const MaterialRange range = MaterialManager()->VariantRange(uint32_t(material));
    auto *const cursor = static_cast<cIVariantIterator *>(range.cursor);
    while (!cursor->IsEqual(range.end)) {
        VariantEntry entry;
        cursor->Get(&entry);
        const uint32_t *const record = VirtualSlot<VariantRecordFn>(entry.variant, 9)(entry.variant);
        if (record[0] != 0) render->IsSupported(reinterpret_cast<const uint8_t *>(record)[0xc]);
        cursor->Next();
    }
    if (range.end) range.end->Release();
    if (range.begin) range.begin->Release();
    if (range.cursor) range.cursor->Release();
}

// The slots return what the originals leave in eax, which no caller reads (zero here).
uint32_t cShape::BindMaterial(const char *material, const char *subset)
{
    BindSubsetMaterial(this, material, subset);
    BindPartMaterials(this);
    MaterialBindingsUpdated event;
    NotifyListeners(MaterialBindingsUpdated::kEventId, 0, &event);
    return 0;
}
// 35 (0xa5f620): the material bound to every subset name of every part.
uint32_t cShape::BindMaterialToAllSubsets(uint32_t material)
{
    for (size_t i = 0; i < mParts.size(); ++i) {
        cGeometrySubsetRange range{};
        static_cast<cIGeometryNode *>(mParts[i].mObject)->GetSubsetNames(&range);
        auto *const cursor = static_cast<cIGeometrySubsetIterator *>(range.mCursor);
        while (!cursor->IsEqual(static_cast<cIGeometrySubsetIterator *>(range.mEnd))) {
            const uint32_t name = cursor->Get();
            Face()->BindMaterial(reinterpret_cast<const char *>(static_cast<uintptr_t>(material)),
                                 reinterpret_cast<const char *>(static_cast<uintptr_t>(name)));
            cursor->Next();
        }
        if (range.mEnd) range.mEnd->Release();
        if (range.mBegin) range.mBegin->Release();
        if (range.mCursor) range.mCursor->Release();
    }
    return 0;
}
uint32_t cShape::AddRenderGroup(const char *group, const char *subset)
{
    const uint32_t bit = static_cast<uint8_t>(RenderGroupRegistry()->BitOfName(group));
    Face()->SetRenderGroupMask(1u << (bit & 0x1f), subset);
    return 0;
}
uint32_t cShape::SetRenderGroupMask(uint32_t mask, const char *subset)
{
    OwnedCell key;
    OpenNameCell(key.cell, subset);
    mBindings[key].mMask |= mask;
    BindPartMaterials(this);
    return 0;
}
uint32_t cShape::RemoveRenderGroup(const char *group, const char *subset)
{
    const uint32_t bit = static_cast<uint8_t>(RenderGroupRegistry()->BitOfName(group));
    Face()->ClearRenderGroupMask(1u << (bit & 0x1f), subset);
    return 0;
}
uint32_t cShape::ClearRenderGroupMask(uint32_t mask, const char *subset)
{
    OwnedCell key;
    OpenNameCell(key.cell, subset);
    mBindings[key].mMask &= ~mask;
    BindPartMaterials(this);
    return 0;
}
uint32_t cShape::SetSubsetWord(uint32_t word, const char *subset)
{
    OwnedCell key;
    OpenNameCell(key.cell, subset);
    mBindings[key].mWord = word;
    BindPartMaterials(this);
    return 0;
}
// 41 (0xa5c520): a part already there with this object is left alone; else the level's part goes (slot 42), the part is
// added in level order (0xa5b600 sorts, the one new part last), listens to the object (slot 36) and the materials are bound.
uint32_t cShape::AddPart(cIGZUnknown *object, int32_t lod)
{
    if (!mParts.empty()) {
        const auto found = FindPartRange(lod);
        if (found != mParts.end() && found->mObject == object) return 0;
    }
    Face()->RemovePart(lod);
    cIObjectGraphNode *const node = Face();
    NotifySubordinateAdding(node, kPartIid);
    {
        const ShapePart part(lod, object);
        mParts.push_back(part);
        SortParts(mParts);
        static_cast<cIGeometryNode *>(object)->AddListener(Face());
        BindPartMaterials(this);
        NotifySubordinateAdded(node, kPartIid, reinterpret_cast<cIObjectGraphNode *>(object));
    }
    return 0;
}
// 42 (0xa5f230): the level's part, if any: its object stops listening, the part goes (later ones shift down) and the
// materials are bound again.
uint32_t cShape::RemovePart(int32_t lod)
{
    if (mParts.empty()) return 0;
    const auto found = FindPartRange(lod);
    if (found == mParts.end()) return 0;
    cIObjectGraphNode *const node = Face();
    NotifySubordinateRemoving(node, kPartIid, reinterpret_cast<cIObjectGraphNode *>(found->mObject));
    static_cast<cIGeometryNode *>(found->mObject)->RemoveListener(Face());
    mParts.erase(found);
    BindPartMaterials(this);
    NotifySubordinateRemoved(node, kPartIid);
    return 0;
}
// 43 (0xa5dd50): with more than one named part, every part by name is loaded and added at its level (the resource manager is
// asked for and dropped first).
uint32_t cShape::LoadNamedParts()
{
    {
        RZAutoRef manager{nullptr};
        GetFrameworkService(0x56b906eu, 0x656b8efcu, manager);
        manager.ReleaseObject();
    }
    if (mPartNames.size() > 1) {
        for (size_t i = 0; i < mPartNames.size(); ++i) {
            const OwnedCell held = mPartNames[i].mName;
            const int32_t lod = mPartNames[i].mLod;
            cIGZUnknown *object = nullptr;
            if (LoadNamedPart(held, reinterpret_cast<void **>(&object))) Face()->AddPart(object, lod);
            if (object) object->Release();
        }
    }
    return 0;
}
// 44 (0xa5efe0): with more than one named part, the chain: preferred level, the level to keep, the others removed.
uint32_t cShape::UpdateLods()
{
    if (mPartNames.size() > 1) UpdateLodChain(this);
    return 0;
}
// 45 (0xa5d9c0, ret 4): the variants of every part material bound to the subset's material (every bound material for a
// null subset; nothing when the subset has no binding) prepared for the part's render (0xaeaf30). The loops read the
// part list's end and each part's material count again every step, as the original does.
// The original's eax is what its last step left: with parts, the last part's material count (the loop's last read); with
// none, zero for a null subset (the argument tested), else what closing the key cell left (undefined, as when the subset
// has no binding). Ours returns the defined values and zero for the undefined ones.
uint32_t cShape::PrepareSubsetVariants(const char *subset)
{
    int32_t material = -1;
    if (subset) {
        OwnedCell key;
        MakeNameCell(&key.cell, subset);
        const auto found = mBindings.find(key);
        if (found == mBindings.end()) return 0;
        material = found->second.mMaterial.mIndex;
    }
    if (mParts.empty()) return 0;
    for (size_t p = 0; p < mParts.size(); ++p) {
        ShapePart &part = mParts[p];
        for (uint32_t i = 0; i < part.mMaterials.size(); ++i) {
            const int32_t bound = part.mMaterials[i].mIndex;
            if (material != -1 && bound != material) continue;
            RequestDrawVariants(static_cast<cIGeometryNode *>(part.mObject)->GetRender(), i, bound);
        }
    }
    return static_cast<uint32_t>(mParts.back().mMaterials.size());
}
static std::vector<ShapePart>::iterator PartToShow(cShape *shape); // below, with slot 57
// 46 (0xa5c6f0, ret 0x18): with parts, the part to show (the only one, else PartToShow) hands its object's render, the
// addresses of the part's materials and groups (zero for none groups) and the caller's four words to the sink `**sink` (slot 28, eight
// words); the two words (`*tablePtr`, `*weightsPtr`, when set) then move on by the first part info's words (+0x3c, +0x34) times
// four. (The original's cached-service static at 0x10f6308 is written here and read nowhere.)
uint32_t cShape::QueuePartDraw(uint32_t flags, uint32_t tablePtr, uint32_t weightsPtr, uint32_t transforms, uint32_t sink,
                               uint32_t detailHandle)
{
    if (mParts.empty()) return 0;
    const ShapePart &part = mParts.size() == 1 ? mParts.front() : *PartToShow(this);
    cIGZUnknown *const object = part.mObject;
    if (object == nullptr || part.mMaterials.empty()) return 0;
    const uint32_t materials = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(part.mMaterials.data()));
    const uint32_t groups = part.mGroups.empty() ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(part.mGroups.data()));
    void **const sinkObject = reinterpret_cast<void **>(static_cast<uintptr_t>(sink));
    using SinkFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const SinkFn put = reinterpret_cast<SinkFn *>(*static_cast<void ***>(*sinkObject))[28];
    // (Slot 30 takes no arguments and leaves the stack to the caller, which the original reuses for the next call.)
    const uint32_t render = Word(static_cast<cIGeometryNode *>(object)->GetRender());
    put(*sinkObject, render, flags, materials, tablePtr, weightsPtr, groups, transforms, detailHandle);
    auto *posA = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(tablePtr));
    auto *posB = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(weightsPtr));
    if (*posA != 0) *posA += mFirstPartInfo[2] * 4;
    if (*posB != 0) *posB += mFirstPartInfo[0] * 4;
    return 0;
}
// 0xa5db60: the first part of the level (the parts are sorted by level), the end for none.
std::vector<ShapePart>::iterator cShape::FindPartRange(int32_t lod)
{
    const auto first = std::lower_bound(mParts.begin(), mParts.end(), lod,
                                        [](const ShapePart &part, int32_t value) { return part.mLod < value; });
    return first != mParts.end() && first->mLod == lod ? first : mParts.end();
}
// 47 and 48 (0xa5d430, one body): the object of the part at the level, zero for none.
uint32_t cShape::PartObjectAtLod(uint32_t lod)
{
    if (!mParts.empty()) {
        const auto it = FindPartRange(static_cast<int32_t>(lod));
        if (it != mParts.end()) return Word(it->mObject);
    }
    return 0;
}
uint32_t cShape::PartObjectAtLod2(uint32_t lod) { return PartObjectAtLod(lod); }
uint32_t cShape::FirstPartObject() { return reinterpret_cast<cShapeIShapeSubobject *>(Face())->FirstRecordWord(); }
uint32_t cShape::FirstPartObject2() { return reinterpret_cast<cShapeIShapeSubobject *>(Face())->FirstRecordWord(); }
// 51 (0xa5f540, ret 8): the levels become the `count` words at `levels`.
uint32_t cShape::SetLods(uint32_t count, uint32_t levels)
{
    mLods.resize(count, 0);
    const auto *words = reinterpret_cast<const int32_t *>(static_cast<uintptr_t>(levels));
    for (uint32_t i = 0; i < count; ++i) mLods[i] = words[i];
    return 0;
}
// 0xa5d7f0 (ret 4): eight words for the renderer: the first part's record, its first material, the part count and the
// first part's bounds word (none without parts).
uint32_t cShape::GetRenderInfo(void *out)
{
    auto *words = static_cast<uint32_t *>(out);
    const ShapePart *first = mParts.empty() ? nullptr : &mParts.front();
    uint32_t materials = 0;
    uint32_t bounds = 0;
    if (first) {
        if (!first->mMaterials.empty()) materials = Word(first->mMaterials.data());
        const auto *info = static_cast<cIGeometryNode *>(first->mObject)->GetBoundsInfo();
        bounds = static_cast<const uint32_t *>(info)[3];
    }
    words[0] = mFirstPartInfo[2];
    words[1] = materials;
    words[2] = mFirstPartInfo[0];
    words[3] = mFirstPartInfo[1];
    words[4] = uint32_t(mParts.size());
    words[5] = mFirstPartInfo[3];
    words[6] = mFirstPartInfo[4];
    words[7] = bounds;
    return Word(out);
}
// 53 (0xa5cfd0, ret 4): the name text of the material bound to the subset, zero for none.
uint32_t cShape::MaterialNameOf(const char *subset)
{
    OwnedCell key;
    OpenNameCell(key.cell, subset);
    const auto found = mBindings.find(key);
    if (found == mBindings.end()) return 0;
    const MaterialInfo info = MaterialManager()->GetInfo(found->second.mMaterial.mIndex);
    return info.nameHash; // (the info's first word is the name text)
}
// 54 (0xa5f840, ret 8: returns out): the range of the subset's bindings (cursor, begin, end; each counted for the
// caller), every binding for a null subset. The end iterator is made first. (The original reaches begin's slots without
// a null check: a failed pool allocation faults there, as it would here.)
namespace {
void FillBindingRange(cIGZUnknown **out, cSubsetBindingIterator *begin, cSubsetBindingIterator *end)
{
    cISubsetBindingIterator *const cursor = begin->Clone();
    out[0] = cursor;
    if (cursor) cursor->AddRef();
    out[1] = begin;
    begin->AddRef();
    out[2] = end;
    if (end) end->AddRef();
}
} // namespace
uint32_t cShape::GetSubsetBindings(void *out, const char *subset)
{
    auto *const range = static_cast<cIGZUnknown **>(out);
    if (subset) {
        OwnedCell key;
        MakeNameCell(&key.cell, subset);
        const auto bounds = mBindings.equal_range(key);
        cSubsetBindingIterator *const end = new cSubsetBindingIterator(bounds.second);
        FillBindingRange(range, new cSubsetBindingIterator(bounds.first), end);
    } else {
        cSubsetBindingIterator *const end = new cSubsetBindingIterator(mBindings.end());
        FillBindingRange(range, new cSubsetBindingIterator(mBindings.begin()), end);
    }
    return Word(out);
}
// 0xa5e1d0 (ret 8): the name of the part at a level, as a counted cell written to out (the none-cell when no part has it).
uint32_t cShape::GetPartNameAtLod(uint32_t a, uint32_t b)
{
    auto *out = reinterpret_cast<OwnedCell *>(static_cast<uintptr_t>(a));
    for (const ShapePartName &name : mPartNames) {
        if (name.mLod == static_cast<int32_t>(b)) {
            new (out) OwnedCell(name.mName);
            return a;
        }
    }
    new (out) OwnedCell(CountedCell(0xffffffffu));
    return a;
}

// 56 (0xa5dca0): the vector of words (begin, end, capacity) becomes the parts' levels.
uint32_t cShape::GetPartLods(uint32_t out)
{
    // The caller owns the vector and its code frees it: the blocks come from the allocator service, as the original grows
    // it, never operator new (that crashed CAS sim creation, 2026-11-01: the game's free of a CRT block).
    using LevelVector = std::vector<int32_t, GZServiceAllocator<int32_t>>;
    static_assert(sizeof(LevelVector) == 12, "begin, end, capacity");
    auto &levels = *reinterpret_cast<LevelVector *>(static_cast<uintptr_t>(out));
    levels.resize(mParts.size());
    for (size_t i = 0; i < mParts.size(); ++i) levels[i] = mParts[i].mLod;
    return 0;
}
// 0xa5daa0 (inlined in slot 57): the part to show with several parts: the one of the preferred level, or the last one
// before it (the first when none comes before).
static std::vector<ShapePart>::iterator PartToShow(cShape *shape)
{
    const int32_t level = static_cast<int32_t>(PreferredLod());
    auto it = shape->FindPartRange(level);
    if (it == shape->mParts.end()) {
        it = std::lower_bound(shape->mParts.begin(), shape->mParts.end(), level,
                              [](const ShapePart &part, int32_t value) { return part.mLod < value; });
        if (it != shape->mParts.begin()) --it;
    }
    return it;
}
// 57 (0xa5dc60): the level of the part to show: none (-1) without parts, the only one's, else the part to show.
uint32_t cShape::GetShownLod()
{
    if (mParts.empty()) return 0xffffffffu;
    if (mParts.size() == 1) return static_cast<uint32_t>(mParts.front().mLod);
    return static_cast<uint32_t>(PartToShow(this)->mLod);
}
uint32_t cShape::QueryPair(void *out, uint32_t a)
{
    return Word(reinterpret_cast<cShapeIShapeSubobject *>(Face())->QueryPair(out, a));
}
namespace {
struct BoundsInfo {
    int32_t count;
    nGZCompGeom::BoundBox *boxes;
};
// A part box counts when no axis has its minimum above its maximum (a NaN does not make it empty).
bool BoxUsable(const nGZCompGeom::BoundBox &box)
{
    return !(box.min.x > box.max.x) && !(box.min.y > box.max.y) && !(box.min.z > box.max.z);
}
const BoundsInfo &FirstPartBounds(cShape *shape)
{
    return *static_cast<const BoundsInfo *>(static_cast<cIGeometryNode *>(shape->mParts.front().mObject)->GetBoundsInfo());
}
void GrowByOrigin(nGZCompGeom::BoundBox *target)
{
    const nGZCompGeom::BoundBox origin{{0, 0, 0}, {0, 0, 0}};
    target->Grow(origin);
}
} // namespace

// 59 (0xa5d590, ret 0x10): grows the box `target` by the first part's bounds boxes (at most `count`) that are usable, each
// carried by that index's rigid transform of `rigids` (0x1c apiece) after the transform `base`; the origin without parts.
uint32_t cShape::GrowBoxByPartsMoved(uint32_t target, uint32_t base, uint32_t count, uint32_t rigids)
{
    auto *const box = reinterpret_cast<nGZCompGeom::BoundBox *>(static_cast<uintptr_t>(target));
    if (mParts.empty()) {
        GrowByOrigin(box);
        return 0;
    }
    const BoundsInfo &info = FirstPartBounds(this);
    int32_t n = std::min<int32_t>(static_cast<int32_t>(count), info.count);
    const auto *rigid = reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(static_cast<uintptr_t>(rigids));
    const auto *from = reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(static_cast<uintptr_t>(base));
    for (int32_t i = 0; i < n; ++i, ++rigid) {
        if (!BoxUsable(info.boxes[i])) continue;
        nGZCompGeom::RigidTransform3 placed = *rigid;
        placed.Compose(from);
        nGZCompGeom::ConvexBox moved;
        static_cast<nGZCompGeom::BoundBox &>(moved) = info.boxes[i];
        box->Grow(*moved.TransformRigid(&placed));
    }
    return 0;
}
// 60 (0xa5d4f0, ret 4): grows the box `target` by the first part's usable bounds boxes; the origin without parts.
uint32_t cShape::GrowBoxByParts(uint32_t target)
{
    auto *const box = reinterpret_cast<nGZCompGeom::BoundBox *>(static_cast<uintptr_t>(target));
    if (mParts.empty()) {
        GrowByOrigin(box);
        return 0;
    }
    const BoundsInfo &info = FirstPartBounds(this);
    for (int32_t i = 0; i < info.count; ++i)
        if (BoxUsable(info.boxes[i])) box->Grow(info.boxes[i]);
    return 0;
}

} // namespace nGZSceneGraph
