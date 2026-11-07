#pragma once
// cGZSndEaRenderer (lane KJ): the EA sound renderer service (service id 0xe99b3207) in the standalone shape. It wraps EA's
// sound library (the cdecl / thiscall functions at 0xe7362..0xe78bc, original callees nobody has replaced) and keeps the 3D
// listener: position, orientation (top / front), three factors and a 3 x 3 matrix rebuilt on the renderer's update period
// when a setter marked it dirty, after which the sound renderer service (0x23cecba0) is asked to notify its sounds.
//
// Creation: the factory 0xb12be6 (new 0xc8, the constructor 0xb12ce3) is registered by the sound EA COM director
// (0xb12b68, class 0x499b183a) and hooked to Create; the constructor is reached only from it, and the vtables (0xe7ff6c,
// 0xe7ffa8, 0xe7ffbc, 0xe7ffe0, 0xe80048, 0xe80078, 0xe800b0) only from the constructor and the destructor 0xb12de1, so every
// object is ours and its slots are in effect through that hook. Slot comments give the original's function; a slot whose
// meaning is unknown keeps its number in its name.
#include "audio/AudioInterfaces.h" // cIGZSndUpdateTarget
#include "audio/cTSAudioResourceManager.h" // AudioRef
#include "gz/cIGZSystemService.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct cGZSndVec3 {
    float x, y, z;
};

// cIGZSndRenderer (iid 0xc9d25f29; also the renderer's cIGZUnknown, iid 1): the master level and the pause flag.
class cIGZSndRenderer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xc9d25f29;
    virtual bool SetLevel(int32_t level) = 0;     // 3 (0xb13a6e, ret 4)
    virtual int32_t GetLevel() = 0;               // 4 (0xb13102)
    virtual bool Pause(uint32_t unused) = 0;      // 5 (0xb133e4, ret 4)
    virtual bool Resume() = 0;                    // 6 (0xb13d1d)
    virtual bool IsPaused() = 0;                  // 7 (shared_vf_4e5020: the byte at +0xac of this face)
};

// cIGZSndEaRenderer (iid 0x899b182d): EA library calls and the listener's vectors and matrix. The EA slots return whether
// the library call succeeded (its result >= 0).
class cIGZSndEaRenderer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x899b182d;
    virtual bool Init() = 0;                                              // 3 (thunk 0xb133b8)
    virtual bool Shutdown() = 0;                                          // 4 (thunk 0xb13ce7)
    virtual bool EaSlot5() = 0;                                           // 5 (0xb133c0): EA 0xe746e(1, &flag)
    virtual bool EaSlot6(uint32_t value) = 0;                             // 6 (0xb13cef): 0xe7768 inside 0xe7362 / 0xe7370
    virtual bool EaSlot7() = 0;                                           // 7 (0xb13d08): the same with 0
    virtual void GetMatrix(float *out9) = 0;                              // 8 (0xb13268)
    virtual void SetFactors(float a, float b, float c) = 0;               // 9 (0xb136af, ret 0xc)
    virtual void GetFactors(float *a, float *b, float *c) = 0;            // 10 (0xb13082, ret 0xc)
    virtual bool EaSlot11(uint32_t value) = 0;                            // 11 (0xb12ea3): EA 0xe746e(value, &flag)
    virtual bool EaSlot12(uint32_t value) = 0;                            // 12 (0xb13b5d): slot 11, then EA 0xe74b2
    virtual bool EaSlot13(uint32_t *out) = 0;                             // 13 (0xb131a5): EA 0xe74f7
    virtual bool EaSlot14(uint32_t value, uint32_t *out) = 0;             // 14 (0xb12eca): EA 0xe7506(0, value, out)
    virtual bool EaSlot15(uint32_t value) = 0;                            // 15 (0xb13b7c): slot 14, then EA 0xe754c
    virtual bool EaSlot16(uint32_t *out) = 0;                             // 16 (0xb131c4): EA 0xe75ab(0, out)
    virtual bool EaSlot17(uint32_t value, uint32_t unused) = 0;           // 17 (0xb12ef0): EA 0xe75e1
    virtual bool EaSlot18(uint32_t a, uint32_t b) = 0;                    // 18 (0xb13afa): EA 0xe7617
    virtual bool EaSlot19(uint32_t value, uint32_t *out) = 0;             // 19 (0xb13133): EA 0xe7668
    virtual bool EaSlot20(uint32_t value) = 0;                            // 20 (0xb13ae8): EA 0xe7442
    virtual void SetPositionB(const cGZSndVec3 *v) = 0;                   // 21 (0xb1359a, ret 4)
    virtual void SetListener(const cGZSndVec3 *position, const cGZSndVec3 *top, const cGZSndVec3 *front) = 0; // 22 (0xb13746)
    virtual void GetPositionB(cGZSndVec3 *out) = 0;                       // 23 (0xb13071)
    virtual void GetListener(cGZSndVec3 *position, cGZSndVec3 *top, cGZSndVec3 *front) = 0; // 24 (0xb130cf)
};

class cIGZSnd3DListenerEx;

// cIGZSnd3DListener (iid 0x406077d2): position and orientation; every out pointer may be null.
class cIGZSnd3DListener : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x406077d2;
    virtual bool Init() = 0;                                              // 3 (thunk 0xb133b0)
    virtual bool Shutdown() = 0;                                          // 4 (thunk 0xb13cdf)
    virtual void ListenerSlot5() = 0;                                     // 5 (shared_vf_140fe0: does nothing)
    virtual void GetPosition(float *x, float *y, float *z) = 0;           // 6 (0xb131e6, ret 0xc)
    virtual bool SetPosition(float x, float y, float z) = 0;              // 7 (0xb13ba5, ret 0xc)
    virtual void GetOrientation(float *topX, float *topY, float *topZ, float *frontX, float *frontY,
                                float *frontZ) = 0;                       // 8 (0xb13156, ret 0x18)
    virtual bool SetOrientation(float topX, float topY, float topZ, float frontX, float frontY,
                                float frontZ) = 0;                        // 9 (0xb13b11, ret 0x18)
    virtual cIGZSnd3DListenerEx *AsListenerEx() = 0;                      // 10 (0xb12e97)
};

// cIGZSnd3DListenerEx (iid 0x5b30e7b3): the listener's other vector, the factors and two more values.
class cIGZSnd3DListenerEx : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x5b30e7b3;
    virtual void GetVector70(float *x, float *y, float *z) = 0;           // 3 (0xb1327b, ret 0xc)
    virtual bool SetVector70(float x, float y, float z) = 0;              // 4 (0xb13c08, ret 0xc)
    virtual void GetAll(float *px, float *py, float *pz, float *vx, float *vy, float *vz, float *tx, float *ty, float *tz,
                        float *fx, float *fy, float *fz) = 0;             // 5 (0xb12fda, ret 0x30)
    virtual bool SetAll(float px, float py, float pz, float vx, float vy, float vz, float tx, float ty, float tz, float fx,
                        float fy, float fz) = 0;                          // 6 (0xb13506, ret 0x30)
    virtual void GetValueAc(float *out) = 0;                              // 7 (0xb130bc)
    virtual bool SetValueAc(float value) = 0;                             // 8 (0xb13733)
    virtual void GetFactorLength(float *out) = 0;                         // 9 (0xb13210)
    virtual bool SetAllFactors(float value) = 0;                          // 10 (0xb13bde)
    virtual void GetValueB0(float *out) = 0;                              // 11 (0xb130a9)
    virtual bool SetValueB0(float value) = 0;                             // 12 (0xb13720)
};

// cIGZSndEaRendererEffects (iid 0xab79e31c): the voice count and the voice handles, by index.
class cIGZSndEaRendererEffects : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xab79e31c;
    virtual uint32_t GetMaxVoices() = 0;                                  // 3 (0xb13119): EA 0xe7779
    virtual bool SetMaxVoices(uint32_t count) = 0;                        // 4 (0xb13a88): EA 0xe7779 / 0xe7788
    virtual bool OpenVoice(uint32_t index, uint32_t value) = 0;           // 5 (0xb12f0b): EA 0xe779d, 0xe7895
    virtual bool CloseVoice(uint32_t index) = 0;                          // 6 (0xb12f68): EA 0xe78bc
    virtual bool SetVoiceLevel(uint32_t index, float level) = 0;          // 7 (0xb136e3): EA 0xe7811
};

class cGZSndEaRenderer : public cIGZSystemService,
                         public cIGZSndUpdateTarget,
                         public cIGZSndRenderer,
                         public cIGZSndEaRenderer,
                         public cIGZSnd3DListener,
                         public cIGZSnd3DListenerEx,
                         public cIGZSndEaRendererEffects {
public:
    static constexpr uint32_t kSize = 0xc8;
    static constexpr uint32_t kServiceId = 0xe99b3207;
    static constexpr uint32_t kBufferSize = 200000; // Init's EA work buffer

    cGZSndEaRenderer();                      // 0xb12ce3
    virtual ~cGZSndEaRenderer() noexcept;    // 0xb12de1; deleting 0xb12e37 (slot 13)
    cGZSndEaRenderer(const cGZSndEaRenderer &) = delete;
    cGZSndEaRenderer &operator=(const cGZSndEaRenderer &) = delete;

    static cGZSndEaRenderer *__cdecl Create(); // 0xb12be6: new 0xc8, the constructor

    // cIGZUnknown (slots 0-2 of every face; the other faces' are the thunks 0xb13485.. / 0xb12e67.. / 0xb134d6..)
    bool QueryInterface(uint32_t iid, void **out) override; // 0xb133f0
    uint32_t AddRef() override;                             // 0xb12e59
    uint32_t Release() override;                            // 0xb134b5

    // cIGZSystemService
    uint32_t GetServiceID() override { return kServiceId; } // 0xb13262
    void SetServiceID(uint32_t) override {}                  // shared_vf_300a0
    int32_t GetServicePriority() override { return 0; }      // shared_vf_6c6b0
    bool IsServiceRunning() override { return true; }        // shared_vf_144910
    void SetServiceRunning(bool) override {}                 // shared_vf_300a0
    bool Init() override;                                    // 0xb132a5 (also cIGZSndEaRenderer / cIGZSnd3DListener slot 3)
    bool Shutdown() override;                                // 0xb13c35 (also their slot 4)
    bool OnTick(uint32_t) override { return true; }          // shared_vf_148260
    bool OnIdle(uint32_t) override { return true; }          // shared_vf_148260
    int32_t GetServiceTickPriority() override { return 0; }  // shared_vf_6c6b0

    // cIGZSndUpdateTarget
    void Update(uint32_t now) override; // 0xb13d27

    // cIGZSndRenderer
    bool SetLevel(int32_t level) override;
    int32_t GetLevel() override;
    bool Pause(uint32_t unused) override;
    bool Resume() override;
    bool IsPaused() override { return mPaused; }

    // cIGZSndEaRenderer
    bool EaSlot5() override;
    bool EaSlot6(uint32_t value) override;
    bool EaSlot7() override;
    void GetMatrix(float *out9) override;
    void SetFactors(float a, float b, float c) override;
    void GetFactors(float *a, float *b, float *c) override;
    bool EaSlot11(uint32_t value) override;
    bool EaSlot12(uint32_t value) override;
    bool EaSlot13(uint32_t *out) override;
    bool EaSlot14(uint32_t value, uint32_t *out) override;
    bool EaSlot15(uint32_t value) override;
    bool EaSlot16(uint32_t *out) override;
    bool EaSlot17(uint32_t value, uint32_t unused) override;
    bool EaSlot18(uint32_t a, uint32_t b) override;
    bool EaSlot19(uint32_t value, uint32_t *out) override;
    bool EaSlot20(uint32_t value) override;
    void SetPositionB(const cGZSndVec3 *v) override;
    void SetListener(const cGZSndVec3 *position, const cGZSndVec3 *top, const cGZSndVec3 *front) override;
    void GetPositionB(cGZSndVec3 *out) override;
    void GetListener(cGZSndVec3 *position, cGZSndVec3 *top, cGZSndVec3 *front) override;

    // cIGZSnd3DListener
    void ListenerSlot5() override {}
    void GetPosition(float *x, float *y, float *z) override;
    bool SetPosition(float x, float y, float z) override;
    void GetOrientation(float *topX, float *topY, float *topZ, float *frontX, float *frontY, float *frontZ) override;
    bool SetOrientation(float topX, float topY, float topZ, float frontX, float frontY, float frontZ) override;
    cIGZSnd3DListenerEx *AsListenerEx() override;

    // cIGZSnd3DListenerEx
    void GetVector70(float *x, float *y, float *z) override;
    bool SetVector70(float x, float y, float z) override;
    void GetAll(float *px, float *py, float *pz, float *vx, float *vy, float *vz, float *tx, float *ty, float *tz, float *fx,
                float *fy, float *fz) override;
    bool SetAll(float px, float py, float pz, float vx, float vy, float vz, float tx, float ty, float tz, float fx, float fy,
                float fz) override;
    void GetValueAc(float *out) override;
    bool SetValueAc(float value) override;
    void GetFactorLength(float *out) override;
    bool SetAllFactors(float value) override;
    void GetValueB0(float *out) override;
    bool SetValueB0(float value) override;

    // cIGZSndEaRendererEffects
    uint32_t GetMaxVoices() override;
    bool SetMaxVoices(uint32_t count) override;
    bool OpenVoice(uint32_t index, uint32_t value) override;
    bool CloseVoice(uint32_t index) override;
    bool SetVoiceLevel(uint32_t index, float level) override;

    // Helpers with their own bodies in the original.
    bool SetLevelValue(float level);   // 0xb13a4b
    bool GetLevelValue(float *out);    // 0xb130f4
    void RebuildListener();            // 0xb13d42
    static bool __cdecl NotifySound(cIGZUnknown *sound); // 0xb12fa6: the callback RebuildListener hands the service

    volatile int32_t mRefCount = 0;    // 0x1c
    bool mInitialized = false;         // 0x20
    uint8_t mUnknown21[3];             // never written
    std::vector<char> mBuffer;         // 0x24 EA's work buffer (kBufferSize while initialised)
    float mLevel = 0;                  // 0x30
    cGZSndVec3 mTop{0, 1, 0};          // 0x34 (the constants 0x1074ca4)
    cGZSndVec3 mFront{0, 0, 1};        // 0x40 (0x1074cb0)
    uint8_t mUnknown4c[12];            // never written
    cGZSndVec3 mPosition{0, 0, 0};     // 0x58 (0x10fac34, a zero vector nothing writes)
    cGZSndVec3 mPositionB{0, 0, 0};    // 0x64 set with the position by the listener setters, alone by slot 21
    cGZSndVec3 mVector70;              // 0x70 (not set by the constructor)
    float mMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // 0x7c rows of three: (top x front, top, front) by column, inverted
    cGZSndVec3 mFactors{1, 1, 1};      // 0xa0
    float mValueAc = 0;                // 0xac
    float mValueB0 = 0;                // 0xb0
    bool mPaused = false;              // 0xb4
    bool mDirty = false;               // 0xb5 the listener changed: rebuilt on the next Update
    uint8_t mUnknownB6[2];             // never written
    AudioRef mService;                 // 0xb8 the sound renderer service (0x23cecba0 / 0x106077d2) while initialised
    std::vector<void *> mVoices;       // 0xbc EA voice handles by index
};

static_assert(sizeof(cGZSndEaRenderer) == cGZSndEaRenderer::kSize, "cGZSndEaRenderer size");
static_assert(offsetof(cGZSndEaRenderer, mRefCount) == 0x1c, "cGZSndEaRenderer::mRefCount");
static_assert(offsetof(cGZSndEaRenderer, mInitialized) == 0x20, "cGZSndEaRenderer::mInitialized");
static_assert(offsetof(cGZSndEaRenderer, mBuffer) == 0x24, "cGZSndEaRenderer::mBuffer");
static_assert(offsetof(cGZSndEaRenderer, mLevel) == 0x30, "cGZSndEaRenderer::mLevel");
static_assert(offsetof(cGZSndEaRenderer, mTop) == 0x34, "cGZSndEaRenderer::mTop");
static_assert(offsetof(cGZSndEaRenderer, mFront) == 0x40, "cGZSndEaRenderer::mFront");
static_assert(offsetof(cGZSndEaRenderer, mPosition) == 0x58, "cGZSndEaRenderer::mPosition");
static_assert(offsetof(cGZSndEaRenderer, mPositionB) == 0x64, "cGZSndEaRenderer::mPositionB");
static_assert(offsetof(cGZSndEaRenderer, mVector70) == 0x70, "cGZSndEaRenderer::mVector70");
static_assert(offsetof(cGZSndEaRenderer, mMatrix) == 0x7c, "cGZSndEaRenderer::mMatrix");
static_assert(offsetof(cGZSndEaRenderer, mFactors) == 0xa0, "cGZSndEaRenderer::mFactors");
static_assert(offsetof(cGZSndEaRenderer, mValueAc) == 0xac, "cGZSndEaRenderer::mValueAc");
static_assert(offsetof(cGZSndEaRenderer, mValueB0) == 0xb0, "cGZSndEaRenderer::mValueB0");
static_assert(offsetof(cGZSndEaRenderer, mPaused) == 0xb4, "cGZSndEaRenderer::mPaused");
static_assert(offsetof(cGZSndEaRenderer, mDirty) == 0xb5, "cGZSndEaRenderer::mDirty");
static_assert(offsetof(cGZSndEaRenderer, mService) == 0xb8, "cGZSndEaRenderer::mService");
static_assert(offsetof(cGZSndEaRenderer, mVoices) == 0xbc, "cGZSndEaRenderer::mVoices");

// Test entry: the constructor on caller memory (as the original's 0xb12ce3 is called).
struct cGZSndEaRendererAdapters {
    static cGZSndEaRenderer *__fastcall Construct(void *memory, void *edx); // 0xb12ce3 (tests; not hooked)
};
