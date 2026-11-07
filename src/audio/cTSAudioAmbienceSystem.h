#pragma once
// cTSAudioAmbienceSystem (lane KF): the ambience service (class 0x6c1a7e40, interface 0x6c1a7e3d, service id 0x0c1e612a,
// priority -0x2dc6c0). 0x138 bytes, /O1 (the module is built for size).
//
// It owns the ambience scene object (+0x3c, a 0x5c-byte object of vtable 0xdad3ec, constructor 0x23e694), the ambience
// layers (+0x40, a list of raw pointers the original loaders fill), the cached primitives (+0x68, framework class objects
// of interface 0xcbba8c73 by id), three resources (+0x60, +0x64 and the name table +0x74, all of iid 0x4bb0d269), the time
// curve (+0x78) with its two names (+0x88 / +0xa8), a clock (+0xc8, framework class 0xac4da0f7), and two stopwatches: the
// tick watch (+0xd0, whole seconds per tick) and the update budget (+0xe8, 500 units per update). While active (+0x30) the
// update walks every layer over every item of the scene's item list (+0x108 / +0x10c), 500 units of the budget at a time,
// and when both lists are done it asks the scene for a new item list (its slot 10, from the state at +0x48 and the curve's
// value at the clock's time) and lets the layers and primitives update.
//
// Faces (RTTI order): +0 cITSAudioAmbienceSystem (vtable 0xda9db8, 10 slots), +4 cIGZMessageTarget2 (0xda9de4), +8
// cRZSystemService / cIGZSystemService (0xda9df8), +0xc cRZUnknownMT (0xda9e30), +0x24 cITSCheatSystem::cCheatCommand
// (0xda9e4c, 12 slots).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x22460b (the audio director registers it for class 0x6c1a7e40)
// is hooked to Create; the constructor 0x2229db is called only by it, and the vtables are referenced only by the
// constructor and the destructor 0x222c12 (reached only through the deleting destructor 0x222d56 and its thunks), so every
// system carries ours and the slots need no adapters.
#include "audio/cTSAudioResourceManager.h" // AudioRef, AudioResourceString
#include "audio/cTSAudioSoundManager.h"    // AudioCurve
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZStopwatch.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <unordered_map>
#include <vector>

class cRZString;

// nGZCommandParser::cICommand as cITSCheatSystem::cCheatCommand implements it (vtable 0xda502c, 12 slots). The base class
// keeps a word at +4 (slot 3 stores it, slot 11 reads it) and a count at +8 (its own AddRef / Release, which the ambience
// system's face overrides). Slots whose meaning is not known stay numbered; their base bodies are folded with other
// classes' (verified elsewhere).
class cICheatCommand : public cIGZUnknown {
public:
    virtual void Slot3(uint32_t word, uint32_t unused) = 0; // 3 (ret 8): folded 0x1d0b40, stores `word` at +4
    virtual void Slot4(uint32_t unused) = 0;                // 4 (ret 4): folded 0x300a0, does nothing
    virtual const char *GetName() = 0;                      // 5
    virtual const char *GetHelp(uint32_t unused) = 0;       // 6 (ret 4): 0x1d0a53, the description (slot 10)
    virtual bool Slot7() = 0;                               // 7: folded 0x147480, false
    virtual uint32_t Slot8() = 0;                           // 8: folded 0x6c6b0, 0
    virtual ~cICheatCommand() = default;                    // 9: the deleting destructor
    virtual const char *GetDescription() = 0;               // 10
    virtual uint32_t Slot11() = 0;                          // 11: folded 0x147a30, the word at +4
};

// cITSAudioAmbienceSystem (iid 0x6c1a7e3d): slots 3-9. Slots 3 and 4 are also cIGZSystemService's Init / Shutdown (the +8
// face's slots 8 / 9 are the thunks 0x224cdb / 0x2256a5).
class cITSAudioAmbienceSystem : public cIGZUnknown {
public:
    // The ambience state the scene picks items from (+0x48): three words and a float the primitive registry's slot 5
    // gives (0x2254a3 stores them), the curve's last value, the seconds the tick has counted.
    struct State {
        uint32_t words[3];   // +0x48
        float weight;        // +0x54
        float curveValue;    // +0x58
        uint32_t seconds;    // +0x5c
    };

    virtual bool Init() = 0;                                              // 3 (0x224bd4)
    virtual bool Shutdown() = 0;                                          // 4 (0x2254bf)
    virtual void *GetItems() = 0;                                         // 5 (0x224777): the scene's slot 9
    virtual State *GetState() = 0;                                        // 6 (0x225280, 4 bytes)
    virtual uint32_t LookupId(const cRZString *name) = 0;                 // 7 (0x2248ac)
    virtual bool GetPrimitive(uint32_t id, uint32_t iid, void **out) = 0; // 8 (0x22477f)
    virtual float GetScale(uint32_t key) = 0;                             // 9 (0x2249a2)
};

// The ambience layers: raw pointers (the original's list nodes do not count them).
using AmbienceLayerList = std::list<AudioRef>; // holds a reference on each layer
// The scene's item lists: the layers' slot 7 takes each node's value.
using AmbienceItemList = std::list<void *>;
static_assert(sizeof(AmbienceLayerList) == 8 && sizeof(AmbienceLayerList::iterator) == 4, "MSVC 14.40 list");

class cTSAudioAmbienceSystem : public cITSAudioAmbienceSystem,
                               public cIGZMessageTarget2,
                               public rz::cRZSystemService,
                               public cICheatCommand {
public:
    static constexpr uint32_t kSize = 0x138;
    static constexpr uint32_t kServiceId = 0x0c1e612a;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kIid = 0x6c1a7e3d;

    cTSAudioAmbienceSystem();                    // 0x2229db
    ~cTSAudioAmbienceSystem() noexcept override; // 0x222c12; deleting 0x222d56 (+0xc thunk 0x222d4e, +0x24 thunk 0x222d46)
    cTSAudioAmbienceSystem(const cTSAudioAmbienceSystem &) = delete;
    cTSAudioAmbienceSystem &operator=(const cTSAudioAmbienceSystem &) = delete;

    static cTSAudioAmbienceSystem *__cdecl Create(); // 0x22460b: new 0x138, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x225284; thunks 0x2252c3 / 0x2252cb / 0x2252db / 0x2252d3
    uint32_t AddRef() override;                              // 0x2239ab; thunks 0x2239b3 / 0x2239bb / 0x2239cb / 0x2239c3
    uint32_t Release() override;                             // 0x2252e3; thunks 0x2252eb / 0x2252f3 / 0x225303 / 0x2252fb

    // ---- cITSAudioAmbienceSystem / cIGZSystemService ----
    bool Init() override;
    bool Shutdown() override;
    void *GetItems() override;
    State *GetState() override { return &mState; }
    uint32_t LookupId(const cRZString *name) override;
    bool GetPrimitive(uint32_t id, uint32_t iid, void **out) override;
    float GetScale(uint32_t key) override;
    bool OnTick(uint32_t counter) override; // +8 slot 10, 0x225212

    // ---- cIGZMessageTarget2 (+4) ----
    bool DoMessage(void *message) override; // 0x224554

    // ---- cICheatCommand (+0x24) ----
    void Slot3(uint32_t word, uint32_t unused) override;
    void Slot4(uint32_t unused) override;
    const char *GetName() override;        // 0x22520c
    const char *GetHelp(uint32_t unused) override;        // 0x1d0a53 (cCheatCommand's)
    bool Slot7() override;
    uint32_t Slot8() override;
    const char *GetDescription() override; // 0x2244d6
    uint32_t Slot11() override;

    // ---- members the original calls (non-virtual, __thiscall on the primary face) ----
    void Update(uint32_t seconds);  // 0x2256ad: one budget of layer-by-item steps; a new item list when both lists end
    void SetActive(uint32_t active); // 0x225457: tells the layers (their slot 9) when the flag changes
    void ResetCursors();            // 0x225425: both cursors at their list's end
    void Reload();                  // 0x225352: reloads the scene (in live mode), restarts, logs the time it took
    void ClearLayers();             // 0x2244dc: every layer off, shut down and dropped
    void ShutdownPrimitives();      // 0x22530b: every primitive shut down and released, the vector emptied
    bool LoadResource64();          // 0x22515a: the resource at +0x64
    bool LoadNames();               // 0x2250a2: the name table at +0x74 (false when one is held)
    void SetState(const uint32_t *words, float weight); // 0x2254a3
    void LoadCurve(cRZString *name); // 0x22544f: the curve +0x78 from a name (the original 0xc0a86a)

    // The message handlers DoMessage dispatches to (ret 4: the message).
    bool OnStop(cIGZUnknown *message);         // 0x224af6 (0xb4, 0xb9)
    bool OnLoadScene(cIGZUnknown *message);    // 0x224aae (0xb6)
    bool OnLoadLot(cIGZUnknown *message);      // 0x224b1e (0xb3)
    bool OnModeEnter(cIGZUnknown *message);    // 0x224a01 (0x4c3220e7): reload when the payload's slot 9 is 2
    bool OnModeChange(cIGZUnknown *message);   // 0x224b65 (0xedbeead0): reload when the payload's slot 9 is 0
    bool OnReload(cIGZUnknown *message);       // 0x224bca (0xb2b28373)
    bool OnRegistryState(cIGZUnknown *message); // 0x224a68 (0x0b8d81d5): the state from the primitive registry

    cIGZMessageTarget2 *MessageTarget() { return this; }
    cIGZSystemService *Service() { return this; }

    // +0x00 .. +0x0b: the two interface faces; +0x08 .. +0x23: cRZSystemService; +0x24: the cheat command face
    uint32_t mCheatWord = 0;            // 0x28 the cheat command's word (slot 3 / slot 11)
    uint32_t mCheatRefs = 0;            // 0x2c the cheat command's own count (unused: our AddRef / Release override it)
    uint8_t mActive;                    // 0x30
    uint8_t mUnknown31[3];              // 0x31 never written
    std::set<uint32_t> mMessageIds;     // 0x34
    AudioRef mScene;                    // 0x3c the ambience scene (0x23e694's object)
    AmbienceLayerList mLayers;          // 0x40
    State mState;                       // 0x48
    AudioRef mResource60;               // 0x60
    AudioRef mResource64;               // 0x64 (LoadResource64)
    std::vector<AudioRef> mPrimitives;  // 0x68 GetPrimitive's cache
    AudioRef mNames;                    // 0x74 a cIGZPropertySetString of names to ids (LoadNames)
    AudioCurve mCurve;                  // 0x78
    AudioResourceString mCurveName88;   // 0x88 the curve name OnLoadScene loads
    AudioResourceString mCurveNameA8;   // 0xa8 the curve name OnLoadLot loads
    AudioRef mClock;                    // 0xc8 its slot 7: the time the curve is evaluated at
    uint32_t mUnknownCC;                // 0xcc never written
    cRZStopwatch mTickWatch;            // 0xd0 mode 3
    cRZStopwatch mBudgetWatch;          // 0xe8 mode 3
    AmbienceLayerList::iterator mLayerCursor; // 0x100
    AmbienceLayerList::iterator mLayerEnd;    // 0x104
    AmbienceItemList::iterator mItemCursor;   // 0x108
    AmbienceItemList::iterator mItemEnd;      // 0x10c
    float mScaleDivisor = 1.0f;         // 0x110 GetScale's divisor
    std::unordered_map<uint32_t, uint32_t> mScales; // 0x114 GetScale's values by key
    uint32_t mUnknown134;               // 0x134 never written
};

static_assert(sizeof(cTSAudioAmbienceSystem) == cTSAudioAmbienceSystem::kSize, "cTSAudioAmbienceSystem size");
static_assert(offsetof(cTSAudioAmbienceSystem, mCheatWord) == 0x28, "mCheatWord");
static_assert(offsetof(cTSAudioAmbienceSystem, mActive) == 0x30, "mActive");
static_assert(offsetof(cTSAudioAmbienceSystem, mMessageIds) == 0x34, "mMessageIds");
static_assert(offsetof(cTSAudioAmbienceSystem, mScene) == 0x3c, "mScene");
static_assert(offsetof(cTSAudioAmbienceSystem, mLayers) == 0x40, "mLayers");
static_assert(offsetof(cTSAudioAmbienceSystem, mState) == 0x48, "mState");
static_assert(offsetof(cTSAudioAmbienceSystem, mResource60) == 0x60, "mResource60");
static_assert(offsetof(cTSAudioAmbienceSystem, mPrimitives) == 0x68, "mPrimitives");
static_assert(offsetof(cTSAudioAmbienceSystem, mNames) == 0x74, "mNames");
static_assert(offsetof(cTSAudioAmbienceSystem, mCurve) == 0x78, "mCurve");
static_assert(offsetof(cTSAudioAmbienceSystem, mCurveName88) == 0x88, "mCurveName88");
static_assert(offsetof(cTSAudioAmbienceSystem, mCurveNameA8) == 0xa8, "mCurveNameA8");
static_assert(offsetof(cTSAudioAmbienceSystem, mClock) == 0xc8, "mClock");
static_assert(offsetof(cTSAudioAmbienceSystem, mTickWatch) == 0xd0, "mTickWatch");
static_assert(offsetof(cTSAudioAmbienceSystem, mBudgetWatch) == 0xe8, "mBudgetWatch");
static_assert(offsetof(cTSAudioAmbienceSystem, mLayerCursor) == 0x100, "mLayerCursor");
static_assert(offsetof(cTSAudioAmbienceSystem, mItemEnd) == 0x10c, "mItemEnd");
static_assert(offsetof(cTSAudioAmbienceSystem, mScaleDivisor) == 0x110, "mScaleDivisor");
static_assert(offsetof(cTSAudioAmbienceSystem, mScales) == 0x114, "mScales");

// The hooks' entry points.
struct cTSAudioAmbienceSystemAdapters {
    static cTSAudioAmbienceSystem *__fastcall Construct(void *memory, void *edx); // 0x2229db (tests; not hooked)
};
