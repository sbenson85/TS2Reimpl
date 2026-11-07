#pragma once
// cGZUIScriptService (lane KS): the UI's script service (system service id 0x5a356e15, priority -3000000; COM class
// 0xfa3562fb, built by the creator 0x85b2df the framework's COM director registers). 0x64 bytes, built for size (/O1).
// It hands out the UI's script objects by key (slot 4: the script compiled through the script node's slot 8, the
// object of the compiled script's slot 5 asked for interface 0x9a4527cb, its slot 3), keeps a stack of pending script
// objects (the interpreter targets push and pop them), and is the interpreter's target while a script runs (+8, +0xc,
// +0x10 faces).
//
// Faces (RTTI order): +0 cIGZUIScriptService (game vtable 0xe47ff8, 11 slots), +4 cIGZUILegacyScriptService (0xe48028, 5),
// +8 cIGZScriptTarget (0xe48040, 6), +0xc cIGZScriptInterpretingTarget (0xe4805c, 6), +0x10 cIGZScriptCompilingTarget
// (0xe48078, 4), +0x14 cRZSystemService / cIGZSystemService (0xe4808c, 13), +0x18 cRZUnknownMT (0xe480c4, 6).
//
// Standalone shape (docs/standalone-shape.md): the creator 0x85b2df (`new`, then a tail jump to the constructor) is the
// only way to the constructor 0x85b39a, and the vtables are referenced only by that constructor and the destructor
// 0x85b434, so with the creator hooked every object carries ours. Slot names are lane KS's (tentative, from the bodies).
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <utility>
#include <vector>

class cIGZString;

// The key a script is looked up by (16 bytes, copied by value; the second word is the instance the resource manager's
// lookup may replace).
struct cGZUIScriptKey {
    uint32_t mType;
    uint32_t mInstance;
    uint32_t mGroup;
    uint32_t mExtra;
};

// One argument of a compiling target's slot 3 with kind 3: its kind (4 the source object, 5 the value) and the object.
using cGZUIScriptArgs = std::vector<std::pair<int32_t, cIGZUnknown *>>;

// cIGZUIScriptService (iid 0xfa3562fa).
class cIGZUIScriptService : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xfa3562fa;
    // slot 3 (ret 0x14): slot 4's object asked for `iid` (and released).
    virtual bool QueryScriptObject(const cGZUIScriptKey *key, uint32_t loadArg, uint32_t objectArg, uint32_t iid,
                                   void **out) = 0;
    // slot 4 (ret 0xc): the script for `key` (its instance as the resource manager knows it) compiled; the object its slot
    // 5 hands out for `objectArg`, asked for interface 0x9a4527cb: that one's slot 3, counted. Null when anything fails.
    virtual cIGZUnknown *GetScriptObject(const cGZUIScriptKey *key, uint32_t loadArg, uint32_t objectArg) = 0;
    // slots 5-7 (ret 4): the script node's slot 6 / 7 / 8 for `arg` (the node made ready, the +8 face its target first);
    // the result object the run left (handed over, no longer held).
    virtual cIGZUnknown *RunNode6(void *arg) = 0;
    virtual cIGZUnknown *RunNode7(void *arg) = 0;
    virtual cIGZUnknown *CompileNode(void *arg) = 0;
    // slot 8 (ret 0): the script node, made ready (not counted).
    virtual cIGZUnknown *GetScriptNode() = 0;
    // slot 9 (ret 0, folded 0x230120) / 10 (ret 4): the context object.
    virtual cIGZUnknown *GetContext() = 0;
    virtual void SetContext(cIGZUnknown *context) = 0;
};

// cIGZUILegacyScriptService (iid 0x7a4a48ed).
class cIGZUILegacyScriptService : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x7a4a48ed;
    // slot 3 (ret 4): a new script object (class 0xfa450242) given `arg` and pushed on the pending stack.
    virtual bool PushNewScript(uint32_t arg) = 0;
    // slot 4 (ret 4): the object handed to every compiler (counted).
    virtual bool SetCompilerArgument(cIGZUnknown *object) = 0;
};

// cIGZScriptTarget (iid 0x1a2bdeb8).
class cIGZScriptTarget : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x1a2bdeb8;
    virtual bool Attach(void *host) = 0;            // slot 3 (ret 4)
    virtual bool Detach(void *host, bool keep) = 0; // slot 4 (ret 8)
    virtual bool OnText(void *host, cIGZString *text) = 0; // slot 5 (ret 8): the text between tags (cGZScriptTerp)
};

// cIGZScriptInterpretingTarget (iid 0x1a2bdeb9).
class cIGZScriptInterpretingTarget : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x1a2bdeb9;
    virtual bool BeginCompile(void *target, uint32_t classId) = 0;   // slot 3 (ret 8)
    virtual bool EndCompile(void *target, uint32_t) = 0;              // slot 4 (ret 8)
    virtual bool OnAttribute(void *host, uint32_t id, cIGZUnknown *value) = 0; // slot 5 (ret 0xc): a tag's attribute
};

// cIGZScriptCompilingTarget (iid 0x1a2bdeba).
class cIGZScriptCompilingTarget : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x1a2bdeba;
    // slot 3 (ret 0xc): what the compiler reports (see the body).
    virtual bool OnCompile(void *compiler, int32_t event, void *data) = 0;
};

class cGZUIScriptService : public cIGZUIScriptService,
                           public cIGZUILegacyScriptService,
                           public cIGZScriptTarget,
                           public cIGZScriptInterpretingTarget,
                           public cIGZScriptCompilingTarget,
                           public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xe47ff8; // the game's, for tests
    static constexpr uint32_t kLegacyVTable = 0xe48028;
    static constexpr uint32_t kTargetVTable = 0xe48040;
    static constexpr uint32_t kInterpretingVTable = 0xe4805c;
    static constexpr uint32_t kCompilingVTable = 0xe48078;
    static constexpr uint32_t kServiceVTable = 0xe4808c;
    static constexpr uint32_t kUnknownVTable = 0xe480c4;
    static constexpr uint32_t kSize = 0x64;
    static constexpr uint32_t kServiceId = 0x5a356e15;
    static constexpr int32_t kPriority = -3000000;

    cGZUIScriptService();                     // 0x85b39a
    ~cGZUIScriptService() noexcept override;  // 0x85b434; deleting 0x85b48a (+0x18 slot 3, thunk 0x85b482)

    // 0x85b2df (cdecl): a new service (its primary face; null when new failed).
    static cIGZUIScriptService *__cdecl Create();

    // cIGZUnknown (every face; the secondary faces through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0x85b818
    uint32_t AddRef() override;                              // 0x85b4a9 (cRZSystemService's on +0x14)
    uint32_t Release() override;                             // 0x85b8c7

    // cIGZUIScriptService
    bool QueryScriptObject(const cGZUIScriptKey *key, uint32_t loadArg, uint32_t objectArg, uint32_t iid,
                           void **out) override;                                                     // 0x85b6ab
    cIGZUnknown *GetScriptObject(const cGZUIScriptKey *key, uint32_t loadArg, uint32_t objectArg) override; // 0x85b581
    cIGZUnknown *RunNode6(void *arg) override;    // 0x85b776
    cIGZUnknown *RunNode7(void *arg) override;    // 0x85b704
    cIGZUnknown *CompileNode(void *arg) override; // 0x85b73d
    cIGZUnknown *GetScriptNode() override;        // 0x85b7af
    cIGZUnknown *GetContext() override;           // folded 0x230120
    void SetContext(cIGZUnknown *context) override; // 0x85bbaf

    // cIGZUILegacyScriptService
    bool PushNewScript(uint32_t arg) override;              // 0x85bb0c
    bool SetCompilerArgument(cIGZUnknown *object) override; // 0x85bb82

    // cIGZScriptTarget
    bool Attach(void *host) override;            // 0x85b8ff
    bool Detach(void *host, bool keep) override; // 0x85b956
    bool OnText(void *host, cIGZString *text) override; // folded 0x1477b0: true

    // cIGZScriptInterpretingTarget
    bool BeginCompile(void *target, uint32_t classId) override; // 0x85b4e1
    bool EndCompile(void *target, uint32_t) override;           // 0x85b6e8
    bool OnAttribute(void *host, uint32_t id, cIGZUnknown *value) override; // folded 0x147a70: true

    // cIGZScriptCompilingTarget
    bool OnCompile(void *compiler, int32_t event, void *data) override; // 0x85b9ed

    // cIGZSystemService
    bool Init() override;     // 0x85b7bc
    bool Shutdown() override; // 0x85bbdb

    // The pending stack (front = top): 0x85b7f1 pushes mCurrent (counted, then cleared), 0x85b7c9 pops (releasing),
    // 0x85bf7f peeks.
    bool PushCurrent();
    bool PopPending();
    cIGZUnknown *PeekPending() const;
    // 0x85bc39 (lane MC): the script node (a cGZScriptTerp) made when there is none, its vocabulary given the markup
    // and tag names (gz/cGZUIScriptService.size.cpp), this the node's target; true when there is a node.
    bool ReadScriptNode();

    bool mInitialized = false;            // +0x30 Init / Shutdown
    cIGZUnknown *mScriptNode = nullptr;   // +0x34 made by ReadScriptNode (0x85bc39), released by Shutdown
    cIGZUnknown *mInterpreter = nullptr;  // +0x38 class 0xba2e7954 (Attach), released by Detach
    bool mSharedInterpreter = false;      // +0x3c Attach / Detach manage the interpreter when false; set only while
                                          // ReadScriptNode sets the node's target
    std::list<cIGZUnknown *> mPending;    // +0x40 the pending script objects (counted), the front the current one
    cIGZUnknown *mCurrent = nullptr;      // +0x48 the script object being built (not counted)
    bool mCompiling = false;              // +0x4c between BeginCompile and EndCompile / Detach
    uint32_t mCompilerClass = 0;          // +0x50 BeginCompile's class id
    cIGZUnknown *mResult = nullptr;       // +0x54 class 0x5a354595 (Attach): what a run leaves (slots 5-7 hand it over)
    cIGZUnknown *mCompiler = nullptr;     // +0x58 the BeginCompile class's object
    cIGZUnknown *mCompilerArgument = nullptr; // +0x5c SetCompilerArgument (never released: the original leaks it)
    cIGZUnknown *mContext = nullptr;      // +0x60 SetContext
};
static_assert(sizeof(cGZUIScriptService) == cGZUIScriptService::kSize, "cGZUIScriptService size");
static_assert(offsetof(cGZUIScriptService, mInitialized) == 0x30, "cGZUIScriptService layout");
static_assert(offsetof(cGZUIScriptService, mPending) == 0x40, "cGZUIScriptService layout");
static_assert(offsetof(cGZUIScriptService, mContext) == 0x60, "cGZUIScriptService layout");
