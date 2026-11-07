#pragma once
// cTSAudioSoundManager (lane IY): the audio director's sound manager service (service id 0xcba95d2d, priority -0x2dc6c0),
// the front end the game's sound code calls. 0x3ac bytes, /O2. Most of its slots are thread proxies: on the sound renderer's
// thread (the renderer's slot 51 is its thread id) they run the request's worker (a member of this class, still the
// original's: the "impl" RVAs below); on any other thread they build a cTSAudioEvent carrying the arguments and post it
// (slot 17: straight to the renderer's slot 12 on its thread, else onto the pending list the service tick flushes), and
// the renderer hands the event back to HandleEvent on its thread, which runs the same worker.
//
// Faces (RTTI order): +0 cAbstractBehaviorTypeT<cITSAudioSoundManager, nGZEvent::cRefSingleThreaded> (vtable 0xda92e0, 84
// slots; the count at +4, its top bit "active"), +8 cIGZSndEventHandler (0xda9434), +0xc cIGZSndUpdateTarget (0xda9448),
// +0x10 cIGZSndCompletionFunctionObject (0xda945c), +0x14 cIGZMessageTarget2 (0xda9470), +0x18 cIGZSystemService (0xda9484;
// the word +0x1c and the running byte +0x20 after it).
//
// Standalone shape (docs/standalone-shape.md): the factory 0x21a182 (registered by the audio director) is hooked to Create.
// The constructor 0x2188d6 is called only by the factory and the vtables are referenced only by the constructor and the
// destructor 0x218cf9, so every manager carries ours: the slots, the faces' slots and their adjustor thunks are ours in
// effect through those callers.
#include "audio/AudioInterfaces.h"
#include "audio/cTSAudioResourceManager.h" // AudioRef, AudioIdHash, AudioResourceString
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "gz/cIGZSystemService.h"
#include "misc/StartupObjects.h" // ZeroedVector20
#include "rz/cRZThreadSignal.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <unordered_map>
#include <vector>

class cIGZAny;
class cIGZString;
class cITSAudioEvent;

// cIGZSndCompletionFunctionObject (iid 0x1b3637c6): the renderer calls it when a sound it was given finishes.
class cIGZSndCompletionFunctionObject : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x1b3637c6;
    virtual void OnSoundComplete(cIGZUnknown *sound) = 0; // slot 3 (ret 4)
};

// cITSAudioSoundManager with its behaviour base (nGZEvent::cIAbstractBehaviorType): slots 3..14 are the behaviour type's,
// 15..82 the manager's. Argument words documented as "a bool" are passed on raw to the workers and stored as their low byte.
class cITSAudioSoundManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x4ba8123a;
    // ---- the behaviour type (cAbstractBehaviorTypeT) ----
    virtual void OnEvent(uint32_t event, uint32_t a, void *data, uint32_t d) = 0;    // 3 (0x21a4f8)
    virtual bool AskListenersAlias(uint32_t kind, uint32_t a, uint32_t b) = 0;       // 4 (shared 0x1a9f00: slot 6)
    virtual void NotifyListeners(uint32_t kind, uint32_t a, uint32_t b) = 0;         // 5 (0x21b5c4)
    virtual bool AskListeners(uint32_t kind, uint32_t a, uint32_t b) = 0;            // 6 (0x219fff)
    virtual void Ignore7(uint32_t, uint32_t, uint32_t) = 0;                          // 7 (shared 0x377e0: nothing)
    virtual void EventManagerSlot3(uint32_t a, uint32_t b, uint32_t c) = 0;          // 8 (0x2190ba)
    virtual void EventManagerSlot4(uint32_t a, uint32_t b, uint32_t c) = 0;          // 9 (0x219365)
    virtual void Ignore10(uint32_t, uint32_t, uint32_t) = 0;                         // 10 (shared 0x377e0)
    virtual uint32_t IsActive() = 0;                                                 // 11 (shared 0x147cf0: count >> 31)
    virtual uint32_t GetRegisteredCount() = 0;                                       // 12 (0x21aebf)
    virtual void **GetRegisteredIds(void **out) = 0;                                 // 13 (0x218f78)
    virtual uint32_t *GetRegisteredInfo(uint32_t *out, uint32_t id) = 0;             // 14 (0x219082)
    // ---- the manager ----
    virtual bool Init() = 0;                                                         // 15 (0x21a649)
    virtual bool Shutdown() = 0;                                                     // 16 (0x21c8df)
    virtual void PostEvent(cITSAudioEvent *event) = 0;                               // 17 (0x21b0a9)
    virtual void Request18(float a, float b) = 0;                                    // 18 (0x21bc54)
    virtual void Request19(uint32_t a, float b) = 0;                                 // 19 (0x21b8e7)
    virtual void Request20(uint32_t a, float b, float c) = 0;                        // 20 (0x219ef7)
    virtual bool Request21(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) = 0; // 21 (0x219124)
    virtual bool Request22(uint32_t a, uint32_t b, uint32_t c, cIGZUnknown *object) = 0; // 22 (0x219247)
    virtual void Request23(uint32_t a, cIGZUnknown *object) = 0;                     // 23 (0x21c54f)
    virtual void Request24(uint32_t a, cIGZUnknown *object) = 0;                     // 24 (0x21c3b4)
    virtual void Request25(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t flag) = 0; // 25 (0x21cbcf)
    virtual void Request26(uint32_t a, uint32_t b, uint32_t c, uint32_t flag) = 0;   // 26 (0x21cce7)
    virtual void Request27(uint32_t a, uint32_t flag) = 0;                           // 27 (0x21cf79)
    virtual void Request28(uint32_t a, uint32_t flag) = 0;                           // 28 (0x21d047)
    virtual void Request29(uint32_t a, uint32_t flag) = 0;                           // 29 (0x21ceac)
    virtual void Request30(uint32_t a, uint32_t flag, float b) = 0;                  // 30 (0x21afb7)
    virtual void Request31(uint32_t flag, uint32_t a) = 0;                           // 31 (0x21cddb)
    virtual void Request32(uint32_t flag, uint32_t a) = 0;                           // 32 (0x21aeea)
    virtual void Request33(uint32_t a) = 0;                                          // 33 (0x21a0c9)
    virtual void Request34(uint32_t a, const float *position) = 0;                   // 34 (0x21c61f)
    virtual void Request35(uint32_t a, const float *position) = 0;                   // 35 (0x21c484)
    virtual void Request36(uint32_t a, float b, float c) = 0;                        // 36 (0x21c6ea)
    virtual void Request37() = 0;                                                    // 37 (0x21d1c0)
    virtual uint32_t NewInstanceId() = 0;                                            // 38 (0x21a1bd)
    virtual float GetSoundCompletionTime(uint32_t instance) = 0;                     // 39 (0x21a252)
    virtual bool GetSoundEmitter(uint32_t instance, uint32_t master, uint32_t out) = 0; // 40 (0x21a36d)
    virtual bool Query41(uint32_t a) = 0;                                            // 41 (0x21caef)
    virtual void Request42(uint32_t a, uint32_t b, uint32_t c, uint32_t flag) = 0;   // 42 (0x21c7eb)
    virtual void Request43(uint32_t a, uint32_t b, float c, uint32_t flag) = 0;      // 43 (0x21b716)
    virtual void Request44(uint32_t a, float b) = 0;                                 // 44 (0x21bf88)
    virtual void Request45(uint32_t a, float b, float c) = 0;                        // 45 (0x21bd43)
    virtual void Request46(uint32_t a, float b) = 0;                                 // 46 (0x21bb79)
    virtual void Request47(uint32_t a, float b) = 0;                                 // 47 (0x21b9c2)
    virtual void Request48(uint32_t a, float b) = 0;                                 // 48 (0x21c21e)
    virtual void Request49(uint32_t a, float b) = 0;                                 // 49 (0x21c143)
    virtual void Request50(uint32_t a, uint32_t b, int32_t c) = 0;                   // 50 (0x21c063)
    virtual void Request51(uint32_t a, uint32_t flag) = 0;                           // 51 (0x21b81a)
    virtual void Request52(uint32_t a) = 0;                                          // 52 (0x21b65d)
    virtual void Request53(uint32_t flag) = 0;                                       // 53 (0x21c2f9)
    virtual void Request54(uint32_t a, uint32_t key, void *value) = 0;               // 54 (0x21ba9d)
    virtual bool Query55(uint32_t a) = 0;                                            // 55 (0x21aad8)
    virtual bool VoiceServiceSlot11(uint32_t a) = 0;                                 // 56 (0x21aa95)
    virtual bool SetSpeakerMode(uint32_t mode) = 0;                                  // 57 (0x21beb3)
    virtual bool SetSpeakerModeByName(const cIGZString *name) = 0;                   // 58 (0x21be44)
    virtual void Request59(const cIGZString *text, uint32_t a, uint32_t b) = 0;      // 59 (0x21b41b)
    virtual const char *GetSoundName(uint32_t instance, uint32_t resource) = 0;      // 60 (0x21ac60)
    virtual uint32_t GetCategoryLogChannel(uint32_t category) = 0;                   // 61 (0x21abb8)
    virtual uint32_t GetElapsed() = 0;                                               // 62 (shared 0x15e970: +0x24)
    virtual bool Get20A() = 0;                                                       // 63 (0x21aed6)
    virtual bool Get209() = 0;                                                       // 64 (0x219ff8)
    virtual bool Get20B() = 0;                                                       // 65 (0x21b571)
    virtual float Get2D4() = 0;                                                      // 66 (0x21aec8)
    virtual float Get2D8() = 0;                                                      // 67 (0x21aecf)
    virtual float EvaluateCurve1B0(float x) = 0;                                     // 68 (0x21b578): times +0x2dc
    virtual float EvaluateCurve160(float x) = 0;                                     // 69 (0x21b12a)
    virtual float EvaluateCurve170(float x) = 0;                                     // 70 (0x21b5aa)
    virtual float EvaluateCurve180(float x) = 0;                                     // 71 (0x21934b)
    virtual float EvaluateCurve190(float x) = 0;                                     // 72 (0x21a4b1)
    virtual float EvaluateCurve1A0(float x) = 0;                                     // 73 (0x21a497)
    virtual float Get2B8() = 0;                                                      // 74 (0x21b5a3)
    virtual float GetCategoryVolume(uint32_t category) = 0;                          // 75 (0x21a1d5)
    virtual float GetRendererFloat() = 0;                                            // 76 (0x21a206)
    virtual bool Get2C1() = 0;                                                       // 77 (0x21a642)
    virtual uint32_t Get338() = 0;                                                   // 78 (0x21a231)
    virtual float Get33C() = 0;                                                      // 79 (0x21a244)
    virtual float Get340() = 0;                                                      // 80 (0x21a24b)
    virtual float Get2C4() = 0;                                                      // 81 (0x21b59c)
    virtual uint32_t Get344() = 0;                                                   // 82 (0x218f71)
    // 83: the class's deleting destructor (0x218f2f)
};

// The behaviour base's data: the count (its top bit "active").
class cTSAudioSoundManagerBehavior : public cITSAudioSoundManager {
public:
    uint32_t mRefCount = 0; // +4 (0x21883e)
};

// The service face's data: a word nothing reads and the running byte (slots 6 / 7 through the face).
class cTSAudioSoundManagerService : public cIGZSystemService {
public:
    uint32_t mServiceWord = 0; // +0x1c
    uint8_t mRunning = 0;      // +0x20
};

// A volume ramp: the value moves towards the target by rate x elapsed per update (0x21d114).
struct AudioRamp {
    float value, target, rate;
    bool Step(float elapsed);
};
static_assert(sizeof(AudioRamp) == 12);

// LoadSoundDefaults' helpers (cdecl; only LoadSoundDefaults calls them, so in effect through it). `set` is the defaults
// resource's string-keyed property set.
struct SoundDefaults {
    static float __cdecl GetFloat(void *set, const GZServiceString *name, float fallback);     // 0x217fbb
    static uint32_t __cdecl GetUint(void *set, const GZServiceString *name, uint32_t fallback); // 0x218012
    static bool __cdecl ParseUintList(std::vector<uint32_t> *out, const cIGZString *text);      // 0x2180f2
    static bool __cdecl ParseFloatList(std::vector<float> *out, const cIGZString *text);        // 0x21817d
    static bool __cdecl ParseUint(const cIGZString *token, uint32_t *out);                      // 0x23b687
    static bool __cdecl ParseFloat(const cIGZString *token, float *out);                        // 0x23b6af
    static uint32_t *__cdecl CopyTruncated(const float *first, const float *last, uint32_t *out); // 0x218208
};

// The six curves (startupobj::ZeroedVector20: built by its constructor 0xc0a664, the vector freed by the destructor; the
// original's evaluate 0xc0a6a1 and load 0xc0a86a read them).
struct AudioCurve {
    union {
        startupobj::ZeroedVector20 v;
    };
    AudioCurve() { v.Construct(); }
    AudioCurve(const AudioCurve &) = delete;
    AudioCurve &operator=(const AudioCurve &) = delete;
    ~AudioCurve() { v.items.~vector(); }
};
static_assert(sizeof(AudioCurve) == 0x10);

// The final-event signal (cRZThreadSignal, built with 0: auto-reset).
struct AudioSignal {
    cRZThreadSignal s;
    AudioSignal() { s.Construct(0); }
    AudioSignal(const AudioSignal &) = delete;
    AudioSignal &operator=(const AudioSignal &) = delete;
    ~AudioSignal() { s.Destruct(); }
};
static_assert(sizeof(AudioSignal) == 8);

// The embedded cTSAudioResourceMemoryBuffer (0x18 bytes; constructor 0x23b4a8, destructor 0x23b4ce, still the original's).
struct AudioEmbeddedBuffer {
    uint8_t raw[0x18];
    AudioEmbeddedBuffer();
    AudioEmbeddedBuffer(const AudioEmbeddedBuffer &) = delete;
    AudioEmbeddedBuffer &operator=(const AudioEmbeddedBuffer &) = delete;
    ~AudioEmbeddedBuffer();
};

// The name maps: the 64-bit id (instance | resource << 32, hashed by its low word) to a cRZString (nodes 0x30).
using AudioNameMap = std::unordered_map<uint64_t, AudioResourceString, AudioIdHash>;
static_assert(sizeof(AudioNameMap) == 0x20);

class cTSAudioSoundManager : public cTSAudioSoundManagerBehavior,
                             public cIGZSndEventHandler,
                             public cIGZSndUpdateTarget,
                             public cIGZSndCompletionFunctionObject,
                             public cIGZMessageTarget2,
                             public cTSAudioSoundManagerService {
public:
    static constexpr uint32_t kSize = 0x3ac;
    static constexpr uint32_t kServiceId = 0xcba95d2d;
    static constexpr int32_t kPriority = int32_t(0xffd23940);
    static constexpr uint32_t kMessageTargetIid = 0x452294aa;
    static constexpr uint32_t kEventHandlerIid = 0xbb523bb2;

    cTSAudioSoundManager();          // 0x2188d6
    virtual ~cTSAudioSoundManager(); // 0x218cf9; slot 83, the deleting destructor 0x218f2f
    cTSAudioSoundManager(const cTSAudioSoundManager &) = delete;
    cTSAudioSoundManager &operator=(const cTSAudioSoundManager &) = delete;

    static cTSAudioSoundManager *__cdecl Create(); // 0x21a182: new 0x3ac, the constructor

    // ---- cIGZUnknown (every face; thunks QI 0x21b1db / 0x21b1fb / 0x21b1e3 / 0x21b1eb / 0x21b1f3, AddRef 0x218f69 /
    // 0x218f51 / 0x218f59 / 0x218f61 (+8: shared_vf_1a9a39), Release 0x21b549 / 0x21b569 / 0x21b551 / 0x21b559 / 0x21b561) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x21b144 (base: shared_vf_1aa007)
    uint32_t AddRef() override;                              // shared_vf_1a9a34 -> shared_vf_1a9a25
    uint32_t Release() override;                             // 0x21b544 -> 0x21b4fb

    // ---- the behaviour type ----
    void OnEvent(uint32_t event, uint32_t a, void *data, uint32_t d) override;
    bool AskListenersAlias(uint32_t kind, uint32_t a, uint32_t b) override;
    void NotifyListeners(uint32_t kind, uint32_t a, uint32_t b) override;
    bool AskListeners(uint32_t kind, uint32_t a, uint32_t b) override;
    void Ignore7(uint32_t, uint32_t, uint32_t) override {}
    void EventManagerSlot3(uint32_t a, uint32_t b, uint32_t c) override;
    void EventManagerSlot4(uint32_t a, uint32_t b, uint32_t c) override;
    void Ignore10(uint32_t, uint32_t, uint32_t) override {}
    uint32_t IsActive() override { return mRefCount >> 31; }
    uint32_t GetRegisteredCount() override;
    void **GetRegisteredIds(void **out) override;
    uint32_t *GetRegisteredInfo(uint32_t *out, uint32_t id) override;

    // ---- the manager ----
    bool Init() override;
    bool Shutdown() override;
    void PostEvent(cITSAudioEvent *event) override;
    void Request18(float a, float b) override;
    void Request19(uint32_t a, float b) override;
    void Request20(uint32_t a, float b, float c) override;
    bool Request21(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) override;
    bool Request22(uint32_t a, uint32_t b, uint32_t c, cIGZUnknown *object) override;
    void Request23(uint32_t a, cIGZUnknown *object) override;
    void Request24(uint32_t a, cIGZUnknown *object) override;
    void Request25(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t flag) override;
    void Request26(uint32_t a, uint32_t b, uint32_t c, uint32_t flag) override;
    void Request27(uint32_t a, uint32_t flag) override;
    void Request28(uint32_t a, uint32_t flag) override;
    void Request29(uint32_t a, uint32_t flag) override;
    void Request30(uint32_t a, uint32_t flag, float b) override;
    void Request31(uint32_t flag, uint32_t a) override;
    void Request32(uint32_t flag, uint32_t a) override;
    void Request33(uint32_t a) override;
    void Request34(uint32_t a, const float *position) override;
    void Request35(uint32_t a, const float *position) override;
    void Request36(uint32_t a, float b, float c) override;
    void Request37() override;
    uint32_t NewInstanceId() override;
    float GetSoundCompletionTime(uint32_t instance) override;
    bool GetSoundEmitter(uint32_t instance, uint32_t master, uint32_t out) override;
    bool Query41(uint32_t a) override;
    void Request42(uint32_t a, uint32_t b, uint32_t c, uint32_t flag) override;
    void Request43(uint32_t a, uint32_t b, float c, uint32_t flag) override;
    void Request44(uint32_t a, float b) override;
    void Request45(uint32_t a, float b, float c) override;
    void Request46(uint32_t a, float b) override;
    void Request47(uint32_t a, float b) override;
    void Request48(uint32_t a, float b) override;
    void Request49(uint32_t a, float b) override;
    void Request50(uint32_t a, uint32_t b, int32_t c) override;
    void Request51(uint32_t a, uint32_t flag) override;
    void Request52(uint32_t a) override;
    void Request53(uint32_t flag) override;
    void Request54(uint32_t a, uint32_t key, void *value) override;
    bool Query55(uint32_t a) override;
    bool VoiceServiceSlot11(uint32_t a) override;
    bool SetSpeakerMode(uint32_t mode) override;
    bool SetSpeakerModeByName(const cIGZString *name) override;
    void Request59(const cIGZString *text, uint32_t a, uint32_t b) override;
    const char *GetSoundName(uint32_t instance, uint32_t resource) override;
    uint32_t GetCategoryLogChannel(uint32_t category) override;
    uint32_t GetElapsed() override { return mElapsed; }
    bool Get20A() override { return mFlag20A; }
    bool Get209() override { return mFlag209; }
    bool Get20B() override { return mFlag20B; }
    float Get2D4() override { return mFloat2D4; }
    float Get2D8() override { return mFloat2D8; }
    float EvaluateCurve1B0(float x) override;
    float EvaluateCurve160(float x) override;
    float EvaluateCurve170(float x) override;
    float EvaluateCurve180(float x) override;
    float EvaluateCurve190(float x) override;
    float EvaluateCurve1A0(float x) override;
    float Get2B8() override { return mFloat2B8; }
    float GetCategoryVolume(uint32_t category) override;
    float GetRendererFloat() override;
    bool Get2C1() override { return mFlag2C1; }
    uint32_t Get338() override { return mFlag2F9 ? 0 : mWord338; }
    float Get33C() override { return mFloat33C; }
    float Get340() override { return mFloat340; }
    float Get2C4() override { return mFloat2C4; }
    uint32_t Get344() override { return mWord344; }

    // ---- cIGZSndEventHandler (+8), cIGZSndUpdateTarget (+0xc), completion (+0x10), cIGZMessageTarget2 (+0x14) ----
    bool HandleEvent(cIGZUnknown *event) override;         // 0x219430
    void Update(uint32_t elapsed) override;                // 0x21d15c
    void OnSoundComplete(cIGZUnknown *sound) override;     // 0x219385
    bool DoMessage(void *message) override;                // 0x219b33

    // ---- cIGZSystemService (+0x18; Init / Shutdown are slots 15 / 16 through the thunks 0x21aa33 / 0x21cae7) ----
    uint32_t GetServiceID() override { return kServiceId; }      // 0x21a225
    void SetServiceID(uint32_t) override {}                      // shared 0x300a0
    int32_t GetServicePriority() override { return kPriority; }  // 0x21a22b (slot 12 too)
    bool IsServiceRunning() override { return *reinterpret_cast<const bool *>(&mRunning); } // shared 0x1480a0 (the byte as is)
    void SetServiceRunning(bool running) override { mRunning = running; } // shared 0x1489f0
    bool OnTick(uint32_t counter) override;                      // 0x21aedd
    bool OnIdle(uint32_t) override { return true; }              // shared 0x148260
    int32_t GetServiceTickPriority() override { return kPriority; }

    // ---- members ----
    void FlushPendingEvents(); // 0x21d5e9
    static void *GetEventManager(); // 0x2190da (cached at 0x10cc528)

    // +0x00 .. +0x23: the faces and the bases' data
    uint32_t mElapsed = 0;            // 0x24 Update adds its argument
    uint32_t mWord28 = 0;             // 0x28 (0x21fc9e counts)
    uint32_t mWord2C = 0;             // 0x2c
    uint32_t mWord30 = 0;             // 0x30
    uint32_t mWord34 = 0;             // 0x34
    int32_t mInt38 = 0;               // 0x38 event 0xed1e3aca's 0x0d1e3ae8
    int32_t mInt3C = 0;               // 0x3c event 0xed1e3aca's 0xcd1e509f
    std::unordered_map<uint32_t, uint32_t> mObjects; // 0x40 id -> object (not counted; GetSoundName's slots 30 / 31)
    std::list<AudioRef> mList60;      // 0x60
    uint32_t mWord68 = 0;             // 0x68
    uint32_t mWord6C = 0;             // 0x6c
    uint32_t mWord70 = 0;             // 0x70
    uint32_t mWords74[9];             // 0x74 zeroed by the constructor's last loop
    AudioRef mRenderer;               // 0x98 the sound renderer (service 0x23cecba0)
    AudioRef mService9C;              // 0x9c service 0x1af85bfb
    AudioRef mVoiceService;           // 0xa0 service 0xe99b3207
    AudioRef mUpdateDemon;            // 0xa4 the TSObject update demon (framework class 0x8d5812a4)
    std::set<uint32_t> mMessageIds;   // 0xa8
    AudioRef mRefB0;                  // 0xb0
    AudioRamp mMasterFade;            // 0xb4 {1, 1, 0}
    uint32_t mWordC0 = 0;             // 0xc0
    float mCategoryVolumes[9];        // 0xc4 1
    uint8_t mCategoryEnabled[9];      // 0xe8 1
    uint8_t mPadF1[3];                // 0xf1 never written
    AudioRamp mCategoryFades[9];      // 0xf4 {1, 1, 0}
    AudioCurve mCurves[6];            // 0x160, 0x170, 0x180, 0x190, 0x1a0, 0x1b0
    AudioResourceString mString1C0;   // 0x1c0
    AudioResourceString mString1E0;   // 0x1e0
    std::list<AudioRef> mPending;     // 0x200 events posted off the renderer's thread
    uint8_t mByte208 = 0;             // 0x208 (DoMessage 0x6d21507b)
    uint8_t mFlag209 = 1;             // 0x209
    uint8_t mFlag20A = 1;             // 0x20a
    uint8_t mFlag20B = 0;             // 0x20b (Init: the reverb test)
    uint32_t mMaxSounds[3] = {};      // 0x20c MaxPlayingSoundsPerPerformanceLevel (LoadSoundDefaults: 8, 16, 24)
    uint32_t mMaxSoundsByGroup[3][9]; // 0x218 ...ByGroup (never written by the constructor; LoadSoundDefaults fills it)
    float mPitchBySpeed[5] = {};      // 0x284 SimSpeedPitchMultipliers
    float mVolumeBySpeed[5];          // 0x298 SimSpeedVolumeMultipliers (never written by the constructor)
    uint32_t mTimerPeriods[3];        // 0x2ac CallbackTimerPeriods (never written by the constructor)
    float mFloat2B8 = 0;              // 0x2b8 SecondaryListenerInfluence
    uint32_t mWord2BC = 2;            // 0x2bc
    uint8_t mFlag2C0 = 1;             // 0x2c0
    uint8_t mFlag2C1 = 0;             // 0x2c1
    uint8_t mUnset2C2[2];             // 0x2c2 never written
    float mFloat2C4 = 1.0f;           // 0x2c4
    AudioSignal mFinalEventSignal;    // 0x2c8
    float mFloat2D0 = 1.0f;           // 0x2d0
    float mFloat2D4 = 1.0f;           // 0x2d4
    float mFloat2D8 = 1.0f;           // 0x2d8
    float mFloat2DC = 1.0f;           // 0x2dc
    uint32_t mForceSampleRates[4] = {}; // 0x2e0 Force{Sfx,Vox,Music,Ambience}SampleRate
    float mAdjustSampleRates[2] = {};   // 0x2f0 Adjust{Sfx,Vox}SampleRate
    uint8_t mFlag2F8 = 1;             // 0x2f8 events to the renderer from OnEvent
    uint8_t mFlag2F9 = 0;             // 0x2f9
    uint8_t mUnset2FA[2];             // 0x2fa never written
    float mFloat2FC = 1.0f;           // 0x2fc
    AudioEmbeddedBuffer mBuffer;      // 0x300 cTSAudioResourceMemoryBuffer
    std::unordered_map<uint32_t, AudioRef> mRefs318; // 0x318
    uint32_t mWord338 = 2;            // 0x338
    float mFloat33C = 1.0f;           // 0x33c
    float mFloat340 = 1.0f;           // 0x340
    uint32_t mWord344;                // 0x344 never written by the constructor
    AudioNameMap mNames348;           // 0x348
    AudioNameMap mNames368;           // 0x368
    uint8_t mUseNameMaps = 0;         // 0x388
    uint8_t mUnset389[3];             // 0x389 never written
    AudioNameMap mNames38C;           // 0x38c

    // ---- the startup workers (lane IY part 2; hooked) ----
    // 0x21f164 (2629 B): the tuning values from the defaults resource (group 0xebfee33f, type 0x4c1940c5, the instance
    // pair at 0x10cc4c8) through the persist resource manager (slot 5, the set into +0xb0); false (and a log line) when it
    // does not load.
    bool LoadSoundDefaults();
    // 0x220320 (938 B): the user's volume and output settings (the settings service) to slots 19, 51, 52 and 58.
    void ApplySettings();
    // 0x2221e8 (260 B): the speaker setup from the config object +0x318[0xedc89ac8] into +0x338..+0x344.
    void UpdateSpeakerConfig();
    // 0x222147 (161 B): updates every sound in +0x60 and counts the playing ones into +0x68..+0x98.
    void UpdateSounds(uint32_t elapsed);
    // 0x21e9ab (58 B) / 0x21dc23 (61 B): the config object +0x318[key]'s slot 10 (has) / slot 9 (get) for property id.
    bool HasConfigProperty(uint32_t key, uint32_t id);
    bool GetConfigProperty(uint32_t key, uint32_t id, cIGZAny *out);

private:
    bool OnRendererThread();
    cITSAudioEvent *NewEvent(uint32_t id);
    template <class Fill> void Post(uint32_t id, Fill fill);
    template <class Fill> bool Send(uint32_t id, Fill fill);
};

static_assert(sizeof(cTSAudioSoundManager) == cTSAudioSoundManager::kSize, "cTSAudioSoundManager size");
static_assert(offsetof(cTSAudioSoundManager, mRefCount) == 4);
static_assert(offsetof(cTSAudioSoundManager, mServiceWord) == 0x1c);
static_assert(offsetof(cTSAudioSoundManager, mRunning) == 0x20);
static_assert(offsetof(cTSAudioSoundManager, mElapsed) == 0x24);
static_assert(offsetof(cTSAudioSoundManager, mObjects) == 0x40);
static_assert(offsetof(cTSAudioSoundManager, mList60) == 0x60);
static_assert(offsetof(cTSAudioSoundManager, mWords74) == 0x74);
static_assert(offsetof(cTSAudioSoundManager, mRenderer) == 0x98);
static_assert(offsetof(cTSAudioSoundManager, mMessageIds) == 0xa8);
static_assert(offsetof(cTSAudioSoundManager, mMasterFade) == 0xb4);
static_assert(offsetof(cTSAudioSoundManager, mCategoryVolumes) == 0xc4);
static_assert(offsetof(cTSAudioSoundManager, mCategoryEnabled) == 0xe8);
static_assert(offsetof(cTSAudioSoundManager, mCategoryFades) == 0xf4);
static_assert(offsetof(cTSAudioSoundManager, mCurves) == 0x160);
static_assert(offsetof(cTSAudioSoundManager, mString1C0) == 0x1c0);
static_assert(offsetof(cTSAudioSoundManager, mPending) == 0x200);
static_assert(offsetof(cTSAudioSoundManager, mByte208) == 0x208);
static_assert(offsetof(cTSAudioSoundManager, mMaxSoundsByGroup) == 0x218);
static_assert(offsetof(cTSAudioSoundManager, mPitchBySpeed) == 0x284);
static_assert(offsetof(cTSAudioSoundManager, mTimerPeriods) == 0x2ac);
static_assert(offsetof(cTSAudioSoundManager, mAdjustSampleRates) == 0x2f0);
static_assert(offsetof(cTSAudioSoundManager, mFloat2B8) == 0x2b8);
static_assert(offsetof(cTSAudioSoundManager, mFinalEventSignal) == 0x2c8);
static_assert(offsetof(cTSAudioSoundManager, mFloat2D0) == 0x2d0);
static_assert(offsetof(cTSAudioSoundManager, mFlag2F8) == 0x2f8);
static_assert(offsetof(cTSAudioSoundManager, mBuffer) == 0x300);
static_assert(offsetof(cTSAudioSoundManager, mRefs318) == 0x318);
static_assert(offsetof(cTSAudioSoundManager, mWord338) == 0x338);
static_assert(offsetof(cTSAudioSoundManager, mWord344) == 0x344);
static_assert(offsetof(cTSAudioSoundManager, mNames348) == 0x348);
static_assert(offsetof(cTSAudioSoundManager, mNames368) == 0x368);
static_assert(offsetof(cTSAudioSoundManager, mUseNameMaps) == 0x388);
static_assert(offsetof(cTSAudioSoundManager, mNames38C) == 0x38c);

// The hooks' entry points.
struct cTSAudioSoundManagerAdapters {
    static cTSAudioSoundManager *__fastcall Construct(void *memory, void *edx); // 0x2188d6 (tests; not hooked)
};
