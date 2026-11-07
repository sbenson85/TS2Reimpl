#pragma once
// cTSAudioAmbienceManager (lane KG): the ambience sound service (service id 0xabb90bd5, priority 0, interface 0x8bb90bcc).
// 0x2c bytes, /O1 (the module is built for size).
//
// It keeps the ambient sounds it has loaded (a map by instance id, +0x24): each is a framework object of class
// 0x3c1cf59e (interface 0xdc1cf595) loaded from the resource key {0x2026960b, group, instance, high}. Its slots 5-16 are
// thread proxies, as cTSAudioSoundManager's: on the sound renderer's thread (+0x14; the renderer's slot 51 is its thread
// id) they run the request's worker at once; on any other thread they build a cTSAudioEvent carrying the arguments and
// hand it over (slots 5-7: the renderer's slot 8, which answers; slots 8-16: the sound manager's PostEvent), and the
// renderer gives the event back to HandleEvent (the +8 face) on its thread, which runs the same worker. The +0xc face
// (cIExternalSoundSystem) forwards to the sound manager.
//
// Faces (RTTI order): +0 cITSAudioAmbienceManager (vtable 0xdaa870, 18 slots), +4 cIGZSystemService (0xdaa8bc), +8
// cIGZSndEventHandler (0xdaa8f4), +0xc cIExternalSoundSystem (0xdaa908, 10 slots).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x22784f (the audio COM director registers it) is hooked to
// Create; the constructor 0x2274a2 is called only by it, and the vtables are referenced only by the constructor and the
// destructor 0x22755d (reached only through the deleting destructor 0x22767e), so every manager carries ours: the slots,
// their adjustor thunks and the workers are ours in effect through those callers.
#include "audio/AudioInterfaces.h"          // cIGZSndEventHandler
#include "audio/cTSAudioResourceManager.h" // AudioRef
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <map>

class cITSAudioEvent;

// cITSAudioAmbienceManager (iid 0x8bb90bcc). Slots 3 and 4 are also cIGZSystemService's Init / Shutdown (the +4 face's
// slots 8 / 9 are the thunks 0x2278f8 / 0x228448). A sound is named by its instance id and a second word (`high`, the
// resource key's fourth word); the map is keyed by the instance alone. Argument words documented as "a bool" are passed on
// raw to the worker.
class cITSAudioAmbienceManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x8bb90bcc;
    virtual bool Init() = 0;                                                         // 3 (0x227887)
    virtual bool Shutdown() = 0;                                                     // 4 (0x2283ff)
    virtual bool LoadSound(uint32_t group, uint32_t instance, uint32_t high) = 0;    // 5 (0x2279f4)
    virtual bool IsSoundLoaded(uint32_t instance, uint32_t high) = 0;                // 6 (0x227900)
    virtual bool UnloadSound(uint32_t instance, uint32_t high) = 0;                  // 7 (0x2286e6)
    virtual bool Play(uint32_t instance, uint32_t high, uint32_t flag) = 0;     // 8 (0x227ad8): flag a bool
    virtual bool StopSound(uint32_t instance, uint32_t high) = 0;                    // 9 (0x228564)
    virtual void Slot10(uint32_t instance, uint32_t high, float value) = 0;          // 10 (0x228224): sound slot 14, value x10
    virtual void Slot11(uint32_t instance, uint32_t high, float value) = 0;          // 11 (0x228310): sound slot 13, value x10
    virtual void Slot12(uint32_t instance, uint32_t high, float value) = 0;          // 12 (0x228066): sound slot 11, value x1024
    virtual void Slot13(uint32_t instance, uint32_t high, const float *points, uint32_t count) = 0; // 13 (0x227f5d)
    virtual void Slot14(uint32_t instance, uint32_t high, float a, float b) = 0;     // 14 (0x227e4b): sound slot 29
    virtual void Slot15(uint32_t instance, uint32_t high, float value) = 0;          // 15 (0x227d5c): sound manager 48
    virtual void Slot16(uint32_t instance, uint32_t high, float value) = 0;          // 16 (0x227c6d): sound manager 49
    virtual ~cITSAudioAmbienceManager() = default;                                   // 17: the deleting destructor 0x22767e
};

// cIExternalSoundSystem: slots 3-9 forward to the sound manager (cITSAudioSoundManager); each takes a first word it ignores.
class cIExternalSoundSystem : public cIGZUnknown {
public:
    // 3 (0x228450, ret 0x20): a default event filled from the words, the sound manager's slots 22 and 26.
    virtual bool ExternalSlot3(uint32_t a, uint32_t unused, uint32_t b, uint32_t c, uint32_t d, uint32_t flag,
                               uint32_t unused2, float value) = 0;
    virtual bool ExternalSlot4(uint32_t unused, uint32_t a, uint32_t unused2) = 0;          // 4 (0x228633): manager 27
    virtual bool ExternalSlot5(uint32_t unused, uint32_t a, float x, float y, float z) = 0; // 5 (0x2281aa): manager 34
    virtual bool ExternalSlot6(uint32_t unused, uint32_t a, float value) = 0;               // 6 (0x2281fa): manager 36
    virtual bool ExternalSlot7(uint32_t unused, uint32_t a, float value) = 0;               // 7 (0x228186): manager 44
    virtual bool ExternalSlot8(uint32_t unused, uint32_t a, float x, float y) = 0;          // 8 (0x228155): manager 45
    virtual bool ExternalSlot9(uint32_t unused, uint32_t a) = 0;                            // 9 (0x2279cf): manager 41
};

// One loaded sound (the map's value): the names it was loaded under, the sound object, and the last values slots 15 / 16
// gave it.
struct AmbientSound {
    uint32_t instance = 0; // +0
    uint32_t high = 0;     // +4
    AudioRef sound;        // +8 the framework object (iid 0xdc1cf595)
    float value15 = 0;     // +0xc slot 15's value
    float value16 = 0;     // +0x10 slot 16's value
};
static_assert(sizeof(AmbientSound) == 0x14);
// MSVC 14.40 std::map as in the game: node 0x28 (links, colour / nil bytes, the key at +0x10, the value at +0x14).
using AmbientSoundMap = std::map<uint32_t, AmbientSound>;
static_assert(sizeof(AmbientSoundMap) == 8);

class cTSAudioAmbienceManager : public cITSAudioAmbienceManager,
                                public cIGZSystemService,
                                public cIGZSndEventHandler,
                                public cIExternalSoundSystem {
public:
    static constexpr uint32_t kSize = 0x2c;
    static constexpr uint32_t kServiceId = 0xabb90bd5;
    static constexpr uint32_t kHandlerIid = 0xbb523bb2; // cIGZSndEventHandler

    cTSAudioAmbienceManager();                    // 0x2274a2
    ~cTSAudioAmbienceManager() noexcept override; // 0x22755d; deleting 0x22767e
    cTSAudioAmbienceManager(const cTSAudioAmbienceManager &) = delete;
    cTSAudioAmbienceManager &operator=(const cTSAudioAmbienceManager &) = delete;

    static cTSAudioAmbienceManager *__cdecl Create(); // 0x22784f: new 0x2c, the constructor

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x227bbe; thunks 0x227c1c / 0x227c24 / 0x227c2c
    uint32_t AddRef() override;                              // 0x22769d; thunks 0x2276ab / 0x2276b3 / 0x2276bb
    uint32_t Release() override;                             // 0x227c34; thunks 0x227c55 / 0x227c5d / 0x227c65

    // ---- cITSAudioAmbienceManager / cIGZSystemService ----
    bool Init() override;
    bool Shutdown() override;
    bool LoadSound(uint32_t group, uint32_t instance, uint32_t high) override;
    bool IsSoundLoaded(uint32_t instance, uint32_t high) override;
    bool UnloadSound(uint32_t instance, uint32_t high) override;
    bool Play(uint32_t instance, uint32_t high, uint32_t flag) override;
    bool StopSound(uint32_t instance, uint32_t high) override;
    void Slot10(uint32_t instance, uint32_t high, float value) override;
    void Slot11(uint32_t instance, uint32_t high, float value) override;
    void Slot12(uint32_t instance, uint32_t high, float value) override;
    void Slot13(uint32_t instance, uint32_t high, const float *points, uint32_t count) override;
    void Slot14(uint32_t instance, uint32_t high, float a, float b) override;
    void Slot15(uint32_t instance, uint32_t high, float value) override;
    void Slot16(uint32_t instance, uint32_t high, float value) override;

    // cIGZSystemService (+4): the accessors are folded bodies (0x149d00, 0x20daf0, 0x148ba0, 0x2279f0, 0x2281f0, 0x148260)
    uint32_t GetServiceID() override { return mServiceId; }
    void SetServiceID(uint32_t id) override { mServiceId = id; }
    int32_t GetServicePriority() override { return mPriority; }
    bool IsServiceRunning() override { return *reinterpret_cast<const bool *>(&mRunning); } // the byte as is
    void SetServiceRunning(bool running) override { mRunning = running; }
    bool OnTick(uint32_t) override { return true; }
    bool OnIdle(uint32_t) override { return true; }
    int32_t GetServiceTickPriority() override { return mPriority; }

    // ---- cIGZSndEventHandler (+8) ----
    bool HandleEvent(cIGZUnknown *event) override; // 0x2276c3

    // ---- cIExternalSoundSystem (+0xc) ----
    bool ExternalSlot3(uint32_t a, uint32_t unused, uint32_t b, uint32_t c, uint32_t d, uint32_t flag, uint32_t unused2,
                       float value) override;
    bool ExternalSlot4(uint32_t unused, uint32_t a, uint32_t unused2) override;
    bool ExternalSlot5(uint32_t unused, uint32_t a, float x, float y, float z) override;
    bool ExternalSlot6(uint32_t unused, uint32_t a, float value) override;
    bool ExternalSlot7(uint32_t unused, uint32_t a, float value) override;
    bool ExternalSlot8(uint32_t unused, uint32_t a, float x, float y) override;
    bool ExternalSlot9(uint32_t unused, uint32_t a) override;

    // ---- the workers (non-virtual, __thiscall on the primary face): what the slots run on the renderer's thread ----
    bool FindSound(uint32_t instance, uint32_t high, void **out);      // 0x22887c: the sound queried for 0xdc1cf595
    bool DoLoad(uint32_t group, uint32_t instance, uint32_t high);     // 0x228bfe
    bool DoIsLoaded(uint32_t instance, uint32_t high);                 // 0x228bb5
    bool DoUnload(uint32_t instance, uint32_t high);                   // 0x2290d3
    bool DoPlay(uint32_t instance, uint32_t high, uint32_t flag);      // 0x228cc0
    bool DoStop(uint32_t instance, uint32_t high);                     // 0x229039
    void DoSlot10(uint32_t instance, uint32_t high, float value);      // 0x228f77
    void DoSlot11(uint32_t instance, uint32_t high, float value);      // 0x228fd8
    void DoSlot12(uint32_t instance, uint32_t high, float value);      // 0x228f16
    void DoSlot13(uint32_t instance, uint32_t high, const float *points, uint32_t count); // 0x228e3d
    void DoSlot14(uint32_t instance, uint32_t high, float a, float b); // 0x228dd2
    void DoSlot15(uint32_t instance, uint32_t high, float value);      // 0x228d86
    void DoSlot16(uint32_t instance, uint32_t high, float value);      // 0x228d3a
    bool AddSound(uint32_t instance, uint32_t high, cIGZUnknown *sound); // 0x2287b5
    void DoUnloadAll();                                                // 0x22909b
    void UnloadAll();                                                  // 0x22864a: the proxy Shutdown uses

    // The event handlers HandleEvent dispatches to (ret 4: the event, queried for cITSAudioEvent).
    bool OnLoadEvent(cITSAudioEvent *event);     // 0x2288de (0xabbd0bca)
    bool OnIsLoadedEvent(cITSAudioEvent *event); // 0x2288b0 (0x2bbd0bd1)
    bool OnUnloadEvent(cITSAudioEvent *event);   // 0x228b87 (0xebbd0bd6)
    bool OnPlayEvent(cITSAudioEvent *event);     // 0x228919 (0x8bbd0bdc)
    bool OnStopEvent(cITSAudioEvent *event);     // 0x228b51 (0x8bbd0be1)
    void OnSlot10Event(cITSAudioEvent *event);   // 0x228ad5 (0xcbbd0be6)
    void OnSlot11Event(cITSAudioEvent *event);   // 0x228b13 (0x6bbd0bee)
    void OnSlot12Event(cITSAudioEvent *event);   // 0x2289d8 (0x6bbd0c0b)
    void OnSlot13Event(cITSAudioEvent *event);   // 0x228a64 (0xec487275)
    void OnSlot14Event(cITSAudioEvent *event);   // 0x228a16 (0x4c4887ad)
    void OnSlot15Event(cITSAudioEvent *event);   // 0x22899a (0x8c5b5545)
    void OnSlot16Event(cITSAudioEvent *event);   // 0x22895c (0xac5b5550)
    void OnUnloadAllEvent(cITSAudioEvent *event); // 0x228b7f (0x8bc2894d)

    bool OnRendererThread();                        // the renderer's slot 51 against the current thread
    cITSAudioEvent *NewEvent(uint32_t id);          // a counted cTSAudioEvent
    void PostEvent(cITSAudioEvent *event);          // the sound manager's PostEvent, then released

    cIGZSystemService *Service() { return this; }
    cIGZSndEventHandler *Handler() { return this; }
    cIExternalSoundSystem *External() { return this; }

    // +0x00 .. +0x0f: the four faces
    volatile int32_t mRefs = 0;  // 0x10
    AudioRef mRenderer;          // 0x14 the sound renderer (service 0x23cecba0, iid 0x106077d2), held from Init to Shutdown
    uint32_t mServiceId = kServiceId; // 0x18
    int32_t mPriority = 0;       // 0x1c (also the tick priority)
    uint8_t mRunning = 0;        // 0x20
    uint8_t mUnknown21 = 0;      // 0x21 zeroed by the constructor, never read
    uint8_t mUnknown22[2];       // 0x22 never written
    AmbientSoundMap mSounds;     // 0x24 by instance
};

static_assert(sizeof(cTSAudioAmbienceManager) == cTSAudioAmbienceManager::kSize, "cTSAudioAmbienceManager size");
static_assert(offsetof(cTSAudioAmbienceManager, mRefs) == 0x10, "mRefs");
static_assert(offsetof(cTSAudioAmbienceManager, mRenderer) == 0x14, "mRenderer");
static_assert(offsetof(cTSAudioAmbienceManager, mServiceId) == 0x18, "mServiceId");
static_assert(offsetof(cTSAudioAmbienceManager, mPriority) == 0x1c, "mPriority");
static_assert(offsetof(cTSAudioAmbienceManager, mRunning) == 0x20, "mRunning");
static_assert(offsetof(cTSAudioAmbienceManager, mSounds) == 0x24, "mSounds");

// The hooks' entry points.
struct cTSAudioAmbienceManagerAdapters {
    static cTSAudioAmbienceManager *__fastcall Construct(void *memory, void *edx); // 0x2274a2 (tests; not hooked)
};
