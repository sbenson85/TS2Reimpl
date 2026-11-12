#pragma once
// nTSSG::cLightRigManager (lane 261109b, scenegraph row LG7c): the light rigs. The scene graph system makes one (class
// 0x696d3daf through the COM director), adds the rig files (slot 5, "Lights/LightRig.txt" and its overrides), updates
// it each frame (slot 7) and puts the current rig's tree into a lot's scene (slot 11 with slot 12's name). A rig is a
// named record (LightRig) the rig file's commands fill through the manager's parser state (0x2c1800): a resource name,
// the tag of the node in that resource that is the rig, an auto-rig name and the animations by key. Loading a rig
// (0x2c3340) makes its root node, finds the tagged node in the resource and hangs it under the root, and looks the
// animations up; a rig whose node is missing gets a default light (unless it is an auto rig). RVAs 1.18.0.187; all /O2.
//
// Layout (0x34, operator new): cILightRigManager +0 (vtable 0xdb582c, 20 slots), the count +4, the file parser +8, the
// parser state +0xc (counted through its virtual base), the rigs by name +0x10, the current rig's name +0x18, the current
// rig +0x30.
//
// Standalone shape (docs/standalone-shape.md): the factory 0x2c2af0 (the COM director's entry for the class) is hooked to
// ours, so every manager carries our vtable; the slots, constructor and destructors are in effect through it. The parser
// state and its commands are ours too (lane 261109d, sg/cLightRigAndCameraParserState.h): its AddRig fills our map. The
// record constructor 0x2c19b0 stays hooked to ours (LightRig_Constructor below), for any original code left that makes
// records.
#include "gz/cFileParser.h" // FileString, cIFileParser
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class cIGZString;

namespace nGZSceneGraph {
class cICompositionTreeNode;
}

namespace nTSSG {

// The order of a rig's animations by key: case-insensitive (_stricmp), as the parser's insert (0x2c15f0) compares. The
// parser state's map and the rig's are one type (AddRig copies one into the other node for node, 0x2c1310).
struct AnimationKeyLess {
    bool operator()(const std::string &a, const std::string &b) const;
};
using AnimationMap = std::map<std::string, std::string, AnimationKeyLess>;

// A counted pointer released through its object's virtual base (the vbtable at `kVbPtr`, its second entry), as the
// game's cRZAutoRefCount on these faces releases. Destroying releases when non-null.
template <unsigned kVbPtr> struct VirtualBaseRef {
    void *p = nullptr;
    VirtualBaseRef() = default;
    VirtualBaseRef(const VirtualBaseRef &) = delete;
    VirtualBaseRef &operator=(const VirtualBaseRef &) = delete;
    ~VirtualBaseRef();
};

// A counted node pointer (plain slot 2 release when non-null): a rig's root.
struct NodeRef {
    nGZSceneGraph::cICompositionTreeNode *p = nullptr;
    NodeRef() = default;
    NodeRef(const NodeRef &) = delete;
    NodeRef &operator=(const NodeRef &) = delete;
    ~NodeRef();
};

// One rig (0x80; the value of the manager's map, whose nodes are 0xa8). The rig file parser assigns the three names,
// the animations, mAutoRig and mLoaded = mRootMissing = false on the records it makes or finds; Load sets the rest.
struct LightRig {
    LightRig(); // 0x2c19b0: only mLoaded is set (the parser and Update set the flags after it)
    ~LightRig(); // 0x2c1ed0
    LightRig(const LightRig &) = delete;
    LightRig &operator=(const LightRig &) = delete;

    NodeRef mRoot;                                     // +0x00: made by Load (class 0x65246462)
    std::map<std::string, void *> mNamedNodes;         // +0x04: nodes found by tag below the root (GetNamedNode)
    nGZCommandParser::FileString mResourceName;        // +0x0c: the resource holding the rig's tree
    nGZCommandParser::FileString mRootTag;             // +0x2c: the tag of the rig's node in it
    nGZCommandParser::FileString mAutoRigName;         // +0x4c
    AnimationMap mAnimations;                          // +0x6c: animation resource names by key ("_anim" appended)
    std::map<std::string, VirtualBaseRef<4>> mAnimRefs; // +0x74: the animations Load found, by key
    bool mLoaded;                                      // +0x7c
    bool mRootMissing;                                 // +0x7d
    bool mAutoRig;                                     // +0x7e
};
static_assert(offsetof(LightRig, mNamedNodes) == 4 && offsetof(LightRig, mResourceName) == 0xc &&
                  offsetof(LightRig, mRootTag) == 0x2c && offsetof(LightRig, mAutoRigName) == 0x4c &&
                  offsetof(LightRig, mAnimations) == 0x6c && offsetof(LightRig, mAnimRefs) == 0x74 &&
                  offsetof(LightRig, mLoaded) == 0x7c && offsetof(LightRig, mRootMissing) == 0x7d &&
                  offsetof(LightRig, mAutoRig) == 0x7e,
              "LightRig layout");
static_assert(sizeof(LightRig) == 0x80, "LightRig size");

// nTSSG::cILightRigManager (interface id 0x696d3da9; 20 slots, the destructor last).
class cILightRigManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x696d3da9;
    virtual bool Init() = 0;                          // 3 (0x2c2d30)
    virtual bool Shutdown() = 0;                      // 4 (0x2c42d0)
    virtual bool AddRigFile(const char *path) = 0;    // 5 (0x2c41f0, ret 4): the parser's RunFiles result
    virtual void SetDirty() = 0;                      // 6 (0x2c2ae0)
    virtual void Update() = 0;                        // 7 (0x2c3c70): reloads every rig when the files changed
    virtual LightRig *LoadRig(const char *name) = 0;  // 8 (0x2c23b0: a jump to 0x2c3340, ret 4)
    virtual LightRig *SetCurrentRig(const char *name) = 0; // 9 (0x2c4230, ret 4)
    virtual void AddRigToScene(const char *name, void *sceneManager) = 0;      // 10 (0x2c23c0, ret 8)
    virtual void RemoveRigFromScene(const char *name, void *sceneManager) = 0; // 11 (0x2c40a0, ret 8)
    virtual const char *GetCurrentRigName() = 0;      // 12 (0x2c2670): null when empty
    virtual nGZSceneGraph::cICompositionTreeNode *GetRigRoot(const char *name) = 0; // 13 (0x2c3060, ret 4)
    virtual void **GetNamedNode(const char *rig, const char *node) = 0;            // 14 (0x2c30f0, ret 8)
    virtual void *GetAnimation(const char *rig, const char *key) = 0;              // 15 (0x2c2520, ret 8)
    virtual bool HasRig(const char *name) = 0;        // 16 (0x2c2f60, ret 4)
    virtual bool IsRootMissing(const char *name) = 0; // 17 (0x2c2ec0, ret 4)
    virtual bool GetAutoRig(const char *name, cIGZString *autoRigName) = 0; // 18 (0x2c2e00, ret 8)
    virtual ~cILightRigManager() = default;           // 19 (deleting 0x2c2180)
};

class cLightRigManager final : public cILightRigManager {
public:
    using RigMap = std::map<std::string, LightRig>;

    cLightRigManager();           // 0x2c1ab0
    ~cLightRigManager() override; // 0x2c2060
    static cILightRigManager *Create(); // 0x2c2af0: the COM director's factory for class 0x696d3daf

    bool QueryInterface(uint32_t riid, void **out) override; // 0x2c4020
    uint32_t AddRef() override;  // shared 0x148fb0 (reproduced; never hooked)
    uint32_t Release() override; // shared_vf_2c4080 with cRenderTargetTexture (reproduced; never hooked)

    bool Init() override;
    bool Shutdown() override;
    bool AddRigFile(const char *path) override;
    void SetDirty() override;
    void Update() override;
    LightRig *LoadRig(const char *name) override;
    LightRig *SetCurrentRig(const char *name) override;
    void AddRigToScene(const char *name, void *sceneManager) override;
    void RemoveRigFromScene(const char *name, void *sceneManager) override;
    const char *GetCurrentRigName() override;
    nGZSceneGraph::cICompositionTreeNode *GetRigRoot(const char *name) override;
    void **GetNamedNode(const char *rig, const char *node) override;
    void *GetAnimation(const char *rig, const char *key) override;
    bool HasRig(const char *name) override;
    bool IsRootMissing(const char *name) override;
    bool GetAutoRig(const char *name, cIGZString *autoRigName) override;

    // 0x2c3340 (thiscall, ret 4): the rig `name` (made when new), loaded once.
    LightRig *Load(const char *name);
    // The rig `name`, or what the map's head node holds when there is none (FindOrHead's ORIGINAL-BUG).
    LightRig &FindOrHead(const char *name);

    // The file parser (class 0x5cf9459a; plain release).
    struct ParserRef {
        nGZCommandParser::cIFileParser *p = nullptr;
        ParserRef() = default;
        ParserRef(const ParserRef &) = delete;
        ParserRef &operator=(const ParserRef &) = delete;
        ~ParserRef();
    };

    uint32_t mRefs = 0;               // +0x04
    ParserRef mParser;                // +0x08
    VirtualBaseRef<8> mParserState;   // +0x0c: the rig file's parser state (cLightRigAndCameraParserState, 0x248 bytes)
    RigMap mRigs;                     // +0x10
    std::string mCurrentName;         // +0x18
    LightRig *mCurrent = nullptr;     // +0x30
};
static_assert(offsetof(cLightRigManager, mRefs) == 4 && offsetof(cLightRigManager, mParser) == 8 &&
                  offsetof(cLightRigManager, mParserState) == 0xc && offsetof(cLightRigManager, mRigs) == 0x10 &&
                  offsetof(cLightRigManager, mCurrentName) == 0x18 && offsetof(cLightRigManager, mCurrent) == 0x30,
              "cLightRigManager layout");
static_assert(sizeof(cLightRigManager) == 0x34, "cLightRigManager size");

} // namespace nTSSG

// Our compiler's record constructor as the hook target (thiscall, no arguments). The /alternatename is in the .cpp, which
// the anchor pulls in from the library.
extern "C" const uint8_t LightRig_Constructor;
extern "C" const int LightRig_Anchor;
#pragma comment(linker, "/include:_LightRig_Anchor")
