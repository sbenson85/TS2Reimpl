// nTSSG::cLightRigManager (lane 261109b). See cLightRigManager.h.
#include "sg/cLightRigManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "misc/SmallVirtuals.h" // cBranchNodesIIDFilter's vtable 0xe7cf54
#include "rz/StaticGetters.h"   // GetDirectorFrameworkObject
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cLightRigAndCameraParserState.h"
#include "sg/cSceneGraphSystem.h" // cISceneNodeIterator, cSceneNodeRange
#include "sg/cShapeRefNode.h"     // cIShapeRefNode

#include <new>

// The decorated name of the record constructor: the linker fails if it is wrong.
#pragma comment(linker, "/alternatename:_LightRig_Constructor=??0LightRig@nTSSG@@QAE@XZ")

extern "C" const int LightRig_Anchor = 1;

namespace nTSSG {

using nGZSceneGraph::cICompositionTreeNode;
using nGZSceneGraph::cISceneNodeIterator;
using nGZSceneGraph::cIShapeRefNode;

bool AnimationKeyLess::operator()(const std::string &a, const std::string &b) const
{
    return _stricmp(a.c_str(), b.c_str()) < 0;
}

namespace {

constexpr uint32_t kParserClass = 0x5cf9459a;  // the file parser
constexpr uint32_t kParserIid = 0x1cf94583;    // cIFileParser
constexpr uint32_t kRootClass = 0x65246462;    // a rig's root node
constexpr uint32_t kRootIid = 0xe51c10f5;
constexpr uint32_t kAnimClockClass = 0xa9de6c9c; // what a rig's animated face is given when it has animations
constexpr uint32_t kAnimClockIid = 0xdaff38c3;
constexpr uint32_t kLightClass = 0x253d2018;   // the default light of a rig without its node
constexpr uint32_t kLightIid = 0x053d200a;
constexpr uint32_t kLightExtClass = 0xc9c81b9b; // the default light's extension
constexpr uint32_t kLightExtIid = 0xea932e72;
constexpr uint32_t kSceneExtensionIid = 0x3afeef61; // the scene's extension a rig's animated face is added to
constexpr uint32_t kTreeNodeIid = 0x4512dd13;  // cICompositionTreeNode (the resource and the rig's node)
constexpr uint32_t kSearchDepth = 0xf;         // the node finders' depth argument (and the animated face's slot 30)
constexpr uint32_t kParserStateSize = 0x248;

// The object's virtual base face: the vbtable at obj + vbptr, its `entry`th offset.
uint8_t *VirtualBase(void *object, unsigned vbptr, unsigned entry)
{
    uint8_t *const at = static_cast<uint8_t *>(object) + vbptr;
    return at + (*reinterpret_cast<int32_t **>(at))[entry];
}

void VirtualAddRef(void *object, unsigned vbptr) { RawSlot<1>(VirtualBase(object, vbptr, 1)); } // cIGZUnknown AddRef
void VirtualRelease(void *object, unsigned vbptr) { RawSlot<2>(VirtualBase(object, vbptr, 1)); } // cIGZUnknown Release

cIGZCOM *Com() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// The node finders (ours, sg/SGUtilities.h) and services (unreplaced; __cdecl) the rig code calls.
void *AnimatedFace(cICompositionTreeNode *root) // 0xae1ed0 (AnimatedFaceOf): the root's animated face, or null
{
    return nGZSceneGraph::AnimatedFaceOf(root);
}
void *SceneGraphSystem() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); } // slot 23 / 26: resource lookups
void *Service20deb5() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }

// A node range filtered by interface id (slot 44 fills it): the cursor, begin and end iterators, each counted.
struct NodeRange {
    cISceneNodeIterator *mCursor = nullptr;
    cISceneNodeIterator *mBegin = nullptr;
    cISceneNodeIterator *mEnd = nullptr;
    NodeRange() = default;
    NodeRange(const NodeRange &) = delete;
    NodeRange &operator=(const NodeRange &) = delete;
    ~NodeRange()
    {
        if (mEnd) mEnd->Release();
        if (mBegin) mBegin->Release();
        if (mCursor) mCursor->Release();
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); }
};
static_assert(sizeof(NodeRange) == sizeof(nGZSceneGraph::cSceneNodeRange), "NodeRange: a cSceneNodeRange");

// The filter by interface id a node's child range takes: an nRZSceneGraph::cBranchNodesIIDFilter (vtable 0xe7cf54,
// built inline: 0xaea7a0 / 0xaea7e0).
struct TypeFilter {
    const void *mVTable;
    uint32_t mIid;
    explicit TypeFilter(uint32_t iid) : mVTable(&ts2_rva_e7cf54), mIid(iid) {}
    TypeFilter(const TypeFilter &) = delete;
    TypeFilter &operator=(const TypeFilter &) = delete;
    ~TypeFilter() { *static_cast<const void *volatile *>(&mVTable) = &ts2_rva_e7cf54; }
};

// The scene's extension 0x3afeef61 (slots 10 and 11 add a rig's animated face to it or take it out).
// ORIGINAL-BUG: the callers use it without a null check (a scene without one crashes).
void FindSceneExtension(void *sceneManager, VirtualBaseRef<4> &extension)
{
    void *const scene = RawSlot<17, void *>(sceneManager);    // sceneManager: its scene (a cIObjectGraphNode)
    RawSlot<24>(scene, kSceneExtensionIid, &extension.p, 0u); // scene: cIObjectGraphNode::FindExtension
}

} // namespace

template <unsigned kVbPtr> VirtualBaseRef<kVbPtr>::~VirtualBaseRef()
{
    if (p) VirtualRelease(p, kVbPtr);
}

NodeRef::~NodeRef()
{
    if (p) p->Release();
}

cLightRigManager::ParserRef::~ParserRef()
{
    if (p) p->Release();
}

LightRig::LightRig() : mLoaded(false) {}

LightRig::~LightRig() = default;

cLightRigManager::cLightRigManager() = default;

cLightRigManager::~cLightRigManager() = default;

cILightRigManager *cLightRigManager::Create()
{
    return new (std::nothrow) cLightRigManager;
}

bool cLightRigManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cLightRigManager::AddRef()
{
    return ++mRefs;
}

uint32_t cLightRigManager::Release()
{
    if (--mRefs != 0) return mRefs;
    delete this;
    return 0;
}

bool cLightRigManager::Init()
{
    void *const memory = ::operator new(kParserStateSize);
    void *const state = static_cast<cLightRigAndCameraParserState *>(memory)->Construct(this, 1);
    if (void *const old = mParserState.p; state != old) {
        if (state) VirtualAddRef(state, 8);
        mParserState.p = state;
        if (old) VirtualRelease(old, 8);
    }
    cIGZCOM *const com = Com();
    if (nGZCommandParser::cIFileParser *const old = mParser.p) {
        mParser.p = nullptr;
        old->Release();
    }
    com->GetClassObject(kParserClass, kParserIid, reinterpret_cast<void **>(&mParser.p));
    mParser.p->Init(); // ORIGINAL-BUG: no null check (a director without the parser class crashes)
    return true;
}

bool cLightRigManager::Shutdown()
{
    mRigs.clear();
    mParser.p->Shutdown(); // ORIGINAL-BUG: no null check (Shutdown before Init crashes)
    if (nGZCommandParser::cIFileParser *const old = mParser.p) {
        mParser.p = nullptr;
        old->Release();
    }
    if (void *const old = mParserState.p) {
        mParserState.p = nullptr;
        VirtualRelease(old, 8);
    }
    return true;
}

bool cLightRigManager::AddRigFile(const char *path)
{
    void *const state = mParserState.p;
    auto *const face = static_cast<nGZCommandParser::cIBaseParser *>(state ? static_cast<void *>(VirtualBase(state, 8, 2)) : nullptr);
    mParser.p->SetParser(face, 1);
    mParser.p->AddFile(path, 5);
    return mParser.p->RunFiles();
}

void cLightRigManager::SetDirty()
{
    if (mParser.p) mParser.p->SetDirty(true);
}

void cLightRigManager::Update()
{
    if (!mParser.p || !mParser.p->IsDirty()) return;
    for (auto &[name, rig] : mRigs) {
        while (rig.mRoot.p && rig.mRoot.p->HasChildren()) {
            cICompositionTreeNode *const root = rig.mRoot.p;
            root->RemoveChild(root->GetChildren().mCurrent);
        }
        for (auto &named : rig.mNamedNodes) named.second = nullptr;
        rig.mResourceName.value.mString.clear();
        rig.mRootTag.value.mString.clear();
        rig.mAnimations.clear();
        rig.mAnimRefs.clear();
        rig.mAutoRigName.value.mString.clear();
        rig.mLoaded = false;
        rig.mRootMissing = false;
        rig.mAutoRig = false;
    }
    RawSlot<45>(Service20deb5()); // the service 0x20deb5 answers: its slot 45
    mParser.p->RunFiles();
    for (auto &[name, rig] : mRigs) {
        Load(name.c_str());
        for (auto &named : rig.mNamedNodes) GetNamedNode(name.c_str(), named.first.c_str()); // this: slot 14
    }
}

LightRig *cLightRigManager::LoadRig(const char *name)
{
    return Load(name);
}

LightRig *cLightRigManager::SetCurrentRig(const char *name)
{
    mCurrentName.assign(name);
    mCurrent = Load(name);
    return mCurrent;
}

void cLightRigManager::AddRigToScene(const char *name, void *sceneManager)
{
    RawSlot<7>(sceneManager, GetRigRoot(name)); // sceneManager: add the node; this: slot 13
    LightRig &rig = FindOrHead(name);
    if (!AnimatedFace(rig.mRoot.p)) return;
    VirtualBaseRef<4> extension;
    FindSceneExtension(sceneManager, extension);
    RawSlot<6>(extension.p, AnimatedFace(rig.mRoot.p)); // extension (receiver: none; class not in the RTTI found): add (the face asked for again)
}

void cLightRigManager::RemoveRigFromScene(const char *name, void *sceneManager)
{
    RawSlot<9>(sceneManager, GetRigRoot(name)); // sceneManager: remove the node; this: slot 13
    LightRig &rig = FindOrHead(name);
    void *const animated = AnimatedFace(rig.mRoot.p);
    if (!animated) return;
    VirtualBaseRef<4> extension;
    FindSceneExtension(sceneManager, extension);
    RawSlot<7>(extension.p, animated); // extension (receiver: none): remove
}

const char *cLightRigManager::GetCurrentRigName()
{
    return mCurrentName.empty() ? nullptr : mCurrentName.c_str();
}

cICompositionTreeNode *cLightRigManager::GetRigRoot(const char *name)
{
    return FindOrHead(name).mRoot.p;
}

void **cLightRigManager::GetNamedNode(const char *rigName, const char *node)
{
    LightRig &rig = FindOrHead(rigName);
    nGZCommandParser::FileString key{std::string(node)};
    reinterpret_cast<cRZStringHelpers *>(key.RZ())->LowerCase();
    const std::string &lower = key.value.mString;
    auto found = rig.mNamedNodes.find(lower);
    if (found != rig.mNamedNodes.end() && found->second) return &found->second;
    void *&slot = rig.mNamedNodes[lower];
    slot = nGZSceneGraph::FindNodeByName(node, rig.mRoot.p, kSearchDepth);
    return &slot;
}

void *cLightRigManager::GetAnimation(const char *rigName, const char *key)
{
    LightRig &rig = FindOrHead(rigName);
    auto found = rig.mAnimRefs.find(std::string(key));
    return found == rig.mAnimRefs.end() ? nullptr : found->second.p;
}

bool cLightRigManager::HasRig(const char *name)
{
    return mRigs.find(std::string(name)) != mRigs.end();
}

bool cLightRigManager::IsRootMissing(const char *name)
{
    return FindOrHead(name).mRootMissing;
}

bool cLightRigManager::GetAutoRig(const char *name, cIGZString *autoRigName)
{
    LightRig &rig = FindOrHead(name);
    autoRigName->FromChar(rig.mAutoRigName.value.mString.c_str());
    return rig.mAutoRig;
}

// ORIGINAL-BUG: a name with no rig answers the map's head node's value: memory the map allocated for the head and never
// initialised (the slots that take a rig name read its root, flags or names from it).
LightRig &cLightRigManager::FindOrHead(const char *name)
{
    static_assert(sizeof(RigMap) == 8, "the map is its head pointer and size");
    auto found = mRigs.find(std::string(name));
    if (found != mRigs.end()) return found->second;
    uint8_t *const head = *reinterpret_cast<uint8_t **>(&mRigs);
    return *reinterpret_cast<LightRig *>(head + 0x28); // the head node's value (after the links and the key)
}

LightRig *cLightRigManager::Load(const char *name)
{
    LightRig &rig = mRigs[std::string(name)];
    if (!rig.mRoot.p) {
        cIGZCOM *const com = Com();
        if (cICompositionTreeNode *const old = rig.mRoot.p) {
            rig.mRoot.p = nullptr;
            old->Release();
        }
        com->GetClassObject(kRootClass, kRootIid, reinterpret_cast<void **>(&rig.mRoot.p));
    }
    void *const animated = AnimatedFace(rig.mRoot.p);
    if (!rig.mAnimations.empty() && animated) {
        RawSlot<30>(animated, kSearchDepth); // animated: its mode
        VirtualBaseRef<4> clock;
        Com()->GetClassObject(kAnimClockClass, kAnimClockIid, &clock.p);
        RawSlot<17>(animated, clock.p); // animated: its clock
    }
    if (rig.mLoaded) return &rig;
    rig.mLoaded = true;
    rig.mRootMissing = true;

    cICompositionTreeNode *resource = nullptr;
    void *const resources = SceneGraphSystem();
    RawSlot<23>(resources, rig.mResourceName.value.mString.c_str(), 0u, 0u, kTreeNodeIid, reinterpret_cast<void **>(&resource), 0u); // resources: find
    if (resource) {
        if (animated) RawSlot<41>(animated); // animated: begin
        reinterpret_cast<cRZStringHelpers *>(rig.mRootTag.RZ())->LowerCase();
        cICompositionTreeNode *node = nullptr;
        nGZSceneGraph::FindNodeOfKindByName(rig.mRootTag.value.mString.c_str(), resource, kSearchDepth, kTreeNodeIid,
                                            reinterpret_cast<void **>(&node));
        if (node) {
            rig.mRootMissing = false;
            if (node->GetParent()) node->GetParent()->RemoveChild(node);
            rig.mRoot.p->AddChild(node);
            {
                TypeFilter filter(cIShapeRefNode::kIid);
                NodeRange range;
                rig.mRoot.p->MakeIterator44(&range, 0, reinterpret_cast<uintptr_t>(&filter));
                while (!range.AtEnd()) {
                    cIShapeRefNode *shapes = nullptr;
                    range.mCursor->Get()->QueryInterface(cIShapeRefNode::kIid, reinterpret_cast<void **>(&shapes));
                    // ORIGINAL-BUG: the face is used without a null check.
                    while (shapes->ShapeCount()) shapes->RemoveShape(*shapes->GetShapes().mFirst);
                    if (shapes) shapes->Release();
                    range.mCursor->Next();
                }
                for (auto &[key, value] : rig.mAnimations) {
                    nGZCommandParser::FileString animation{value};
                    animation.value.mString.append("_anim");
                    void *found = nullptr;
                    if (RawSlot<26, bool>(SceneGraphSystem(), animation.value.mString.c_str(), &found, 0u)) { // resources: find by name
                        VirtualBaseRef<4> &slot = rig.mAnimRefs[key];
                        if (found != slot.p) {
                            if (found) VirtualAddRef(found, 4);
                            void *const old = slot.p;
                            slot.p = found;
                            if (old) VirtualRelease(old, 4);
                        }
                    }
                    if (found) VirtualRelease(found, 4);
                }
            }
        }
        if (animated) RawSlot<40>(animated, 0u); // animated: end
        if (node) node->Release();
    }
    // ORIGINAL-BUG: mAutoRig is read uninitialised for a rig Load made (the record constructor leaves it, and only the
    // parser and Update set it).
    if (rig.mRootMissing && !rig.mAutoRig) {
        void *light = nullptr;
        Com()->GetClassObject(kLightClass, kLightIid, &light);
        RawSlot<78>(light, -1);  // light: its layer bits
        RawSlot<91>(light, 2u);  // light: its kind
        void *extension = nullptr;
        Com()->GetClassObject(kLightExtClass, kLightExtIid, &extension);
        RawSlot<93>(light, extension);                            // light: its extension
        RawSlot<41>(extension, GameAddress<const float>(0xffd29c)); // extension: its colour (white)
        RawSlot<42>(extension, 0u);                                // extension: the word after
        rig.mRoot.p->AddChild(static_cast<cICompositionTreeNode *>(light));
        if (extension) static_cast<cIGZUnknown *>(extension)->Release();
        if (light) static_cast<cIGZUnknown *>(light)->Release();
    }
    if (resource) resource->Release();
    return &rig;
}

} // namespace nTSSG
