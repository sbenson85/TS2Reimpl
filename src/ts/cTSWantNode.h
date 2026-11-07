#pragma once
// cTSWantNode (lane IL): one node of a want tree (interface cITSWantNode, iid 0x0d82acb6). The tree loader
// (ts/cTSWantTreeLoader.h) builds the nodes, sets their properties (slot 8, from the node's cGZPropertySetString), writes
// their factor weights, flags and ranges directly (the fields below), and links them (slots 4-7); the want simulator reads
// them through the slots. 0xbc bytes; game vtable 0xdd86d8 (38 slots).
//
// Standalone shape (docs/standalone-shape.md): a real class. The constructor 0x5b2750 (hooked: placement new) is the only
// place the game stores the vtable besides Release, so every node carries ours and the slots are in effect through their
// callers. Other code reads and writes the fields directly (the loader, the want simulator), so the layout is the game's.
#include "gz/cIGZPropertySet.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>
#include <map>
#include <vector>

class cITSWantDefinition;
class cITSWantNode;

// A counted node pointer as the game's children vector holds it: a copy AddRefs, an assignment AddRefs the new node and
// releases the old one, and there is no move, so the vector copies on growth (each element AddRef'd into the new block, the
// old ones released) and erase shifts by assignment, as the original's (0x38c9a0, 0x5b3740).
class WantNodeRef {
public:
    explicit WantNodeRef(cITSWantNode *node);
    WantNodeRef(const WantNodeRef &other);
    WantNodeRef &operator=(const WantNodeRef &other);
    ~WantNodeRef();
    cITSWantNode *Get() const { return mNode; }

private:
    cITSWantNode *mNode;
};
static_assert(sizeof(WantNodeRef) == 4, "WantNodeRef layout");

// The children: storage from the allocator service, as the original's.
using WantNodeList = std::vector<WantNodeRef, GZServiceAllocator<WantNodeRef>>;
static_assert(sizeof(WantNodeList) == 0xc, "WantNodeList layout");

// The node's own interface (cITSWantNode, iid 0x0d82acb6): slots 3-37.
class cITSWantNode : public cIGZUnknown {
public:
    virtual void Detach() = 0;                                            // slot 3
    virtual void SetId(uint32_t id) = 0;                                  // slot 4
    virtual void SetParent(cITSWantNode *parent) = 0;                     // slot 5
    virtual void AddChild(cITSWantNode *child) = 0;                       // slot 6
    virtual void RemoveChild(cITSWantNode *child) = 0;                    // slot 7
    virtual void SetProperties(cIGZPropertySetString *set) = 0;           // slot 8
    virtual uint32_t GetNodeId() = 0;                                     // slot 9
    virtual uint32_t GetId() = 0;                                         // slot 10
    virtual cITSWantNode *GetParent() = 0;                                // slot 11
    virtual WantNodeList *GetChildren() = 0;                              // slot 12
    virtual int32_t GetDepth() = 0;                                       // slot 13
    virtual cITSWantDefinition *GetDefinition() = 0;                      // slot 14
    virtual uint32_t GetSubjectType() = 0;                                // slot 15
    virtual uint32_t GetObjectField() = 0;                                // slot 16
    virtual uint32_t GetIntegerField() = 0;                               // slot 17
    virtual uint8_t IsVisible() = 0;                                      // slot 18
    virtual uint8_t InheritsSubject() = 0;                                // slot 19
    virtual uint8_t Regenerates() = 0;                                    // slot 20
    virtual int32_t GetLifetime() = 0;                                    // slot 21
    virtual int32_t GetAdFrequency() = 0;                                 // slot 22
    virtual int32_t GetScore() = 0;                                       // slot 23
    virtual int32_t GetInfluence() = 0;                                   // slot 24
    virtual void GetScoreRange(int32_t *min, int32_t *max) = 0;           // slot 25
    virtual bool HasFactor(int32_t factor) = 0;                           // slot 26
    virtual uint8_t IsSuperZero(int32_t factor) = 0;                      // slot 27
    virtual int32_t GetWeight(int32_t factor) = 0;                        // slot 28
    virtual bool GetAgeFlag(int32_t age) = 0;                             // slot 29
    virtual bool GetAspirationFlag(int32_t aspiration) = 0;               // slot 30
    virtual bool GetPersonalityFlag(int32_t trait) = 0;                   // slot 31
    virtual bool GetPersonDataRange(int32_t *min, int32_t *max) = 0;      // slot 32
    virtual bool GetAspirationScoreRange(int32_t *min, int32_t *max) = 0; // slot 33
    virtual bool GetMoodRange(int32_t *min, int32_t *max) = 0;            // slot 34
    virtual bool GetRelationshipRange(int32_t *min, int32_t *max) = 0;    // slot 35
    virtual int32_t GetPersonDataIndex() = 0;                             // slot 36
    virtual bool Matches(void *instance) = 0;                             // slot 37
};

class cTSWantNode : public cITSWantNode {
public:
    static constexpr uint32_t kIid = 0x0d82acb6;
    static constexpr uint32_t kSize = 0xbc;
    static constexpr int32_t kFactors = 7;
    static constexpr int32_t kNoScoreOverride = 0x42424242;

    cTSWantNode(); // 0x5b2750
    cTSWantNode(const cTSWantNode &) = delete;
    cTSWantNode &operator=(const cTSWantNode &) = delete;

    bool QueryInterface(uint32_t iid, void **out) override; // 0x5b2ec0: 1, kIid
    uint32_t AddRef() override;                             // shared 0x148fb0: ++count at +4
    uint32_t Release() override;                            // 0x5b36a0: at 1 or below the node is destroyed

    void Detach() override;                                  // 0x5b3890
    void SetId(uint32_t id) override;                        // shared 0x470710: +0xa4
    void SetParent(cITSWantNode *parent) override;           // 0x5b3830
    void AddChild(cITSWantNode *child) override;             // 0x5b2940
    void RemoveChild(cITSWantNode *child) override;          // 0x5b3740
    void SetProperties(cIGZPropertySetString *set) override; // 0x5b2ef0
    uint32_t GetNodeId() override;                           // shared 0x148ba0: +0x18
    uint32_t GetId() override;                               // shared 0x155e40: +0xa4
    cITSWantNode *GetParent() override;                      // shared 0x254fd0: +0xa8
    WantNodeList *GetChildren() override;                    // shared 0x5582e0: &+0xac
    int32_t GetDepth() override;                             // shared 0x254680: +0xb8
    cITSWantDefinition *GetDefinition() override;           // 0x5b2af0
    uint32_t GetSubjectType() override;                      // shared 0x15e550: +0x20
    uint32_t GetObjectField() override;                      // shared 0x15e970: +0x24
    uint32_t GetIntegerField() override;                     // shared 0x15f390: +0x28
    uint8_t IsVisible() override;                            // shared 0x15e850: +0x2c
    uint8_t InheritsSubject() override;                      // shared 0x5b3960: +0x2d
    uint8_t Regenerates() override;                          // 0x5b2c10: +0x2e
    int32_t GetLifetime() override;                          // shared 0x1457d0: +0x30
    int32_t GetAdFrequency() override;                       // shared 0x147800: +0x34
    int32_t GetScore() override;                             // 0x5b2ad0
    int32_t GetInfluence() override;                         // 0x5b2a20
    void GetScoreRange(int32_t *min, int32_t *max) override; // 0x5b2a00
    bool HasFactor(int32_t factor) override;                 // 0x5b3940
    uint8_t IsSuperZero(int32_t factor) override;            // 0x5b2c20
    int32_t GetWeight(int32_t factor) override;              // 0x5b2b20
    bool GetAgeFlag(int32_t age) override;                   // 0x5b2b30
    bool GetAspirationFlag(int32_t aspiration) override;     // 0x5b2b90
    bool GetPersonalityFlag(int32_t trait) override;         // 0x5b2c30
    bool GetPersonDataRange(int32_t *min, int32_t *max) override;      // 0x5b2a70
    bool GetAspirationScoreRange(int32_t *min, int32_t *max) override; // 0x5b29d0
    bool GetMoodRange(int32_t *min, int32_t *max) override;            // 0x5b2a40
    bool GetRelationshipRange(int32_t *min, int32_t *max) override;    // 0x5b2aa0
    int32_t GetPersonDataIndex() override;                   // shared 0x24b3f0: +0xa0
    bool Matches(void *instance) override;                   // 0x5b2c90

    // +0x00 the vtable
    int32_t mRefCount = 0;                     // +0x04
    uint32_t mUnused08[4] = {};                // +0x08 cleared by the constructor, never used
    uint32_t mNodeId = 0;                      // +0x18 ("id")
    uint32_t mWantId = 0;                      // +0x1c ("wantID": the want definition's id)
    uint32_t mSubjectType = 0;                 // +0x20 ("subjectType" 0-5, else the definition's object type)
    uint32_t mObjectField = 0;                 // +0x24 ("objectField")
    uint32_t mIntegerField = 0;                // +0x28 ("integerField")
    uint8_t mVisible = 1;                      // +0x2c ("visible")
    uint8_t mInheritSubject = 1;               // +0x2d ("inheritSubject")
    uint8_t mRegeneration = 0;                 // +0x2e ("regeneration")
    int32_t mLifetime = 0;                     // +0x30 ("lifetime", hours: stored in seconds)
    int32_t mAdFrequency = 0;                  // +0x34 ("adFrequency", hours: stored in seconds)
    int32_t mScoreOverride = kNoScoreOverride; // +0x38 ("scoreOverride")
    int32_t mWeights[kFactors] = {};           // +0x3c the factors' weights (the tree loader's <ads weight>)
    uint8_t mSuperZero[kFactors] = {};         // +0x58 (the tree loader's <ads superZero>)
    int32_t mMinScore = 0;                     // +0x60 ("minScore")
    int32_t mMaxScore = 100;                   // +0x64 ("maxScore")
    std::map<int32_t, bool> mAgeFlags;         // +0x68 factor 0
    std::map<int32_t, bool> mAspirationFlags;  // +0x70 factor 1
    std::map<int32_t, bool> mPersonalityFlags; // +0x78 factor 3
    int32_t mMoodRange[2] = {};                // +0x80 factor 2
    int32_t mRelationshipRange[2] = {};        // +0x88 factor 4 ("relationshipMin", "relationshipMax")
    int32_t mAspirationScoreRange[2] = {};     // +0x90 factor 5
    int32_t mPersonDataRange[2] = {};          // +0x98 factor 6
    int32_t mPersonDataIndex = 0;              // +0xa0
    uint32_t mId = 0;                          // +0xa4 the tree id (SetId)
    cITSWantNode *mParent = nullptr;           // +0xa8 counted
    WantNodeList mChildren;                    // +0xac counted
    int32_t mDepth = 0;                        // +0xb8 the parent's depth + 1

private:
    ~cTSWantNode(); // inlined into Release (not virtual)
};
static_assert(sizeof(cTSWantNode) == cTSWantNode::kSize, "cTSWantNode size");

// 0x5507b0 (__stdcall): an integer test by cITSWantDefinition's integer operator (1 ==, 2 <, 3 >, 4 <=, 5 >=; any other
// passes).
bool __stdcall WantCompareIntegers(uint32_t op, int32_t value, int32_t operand);

struct cTSWantNodeAdapters {
    static cTSWantNode *__fastcall Construct(void *memory, void *); // 0x5b2750
};
