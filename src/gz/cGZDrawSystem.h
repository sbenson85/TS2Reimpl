#pragma once
// cGZDrawSystem: the 2D drawing service (id 0xeb2ea3cb; docs/subsystems/rendering.md, R5). It keeps the draw context
// engine factories (a cGZDrawContextRenderToBufferFactory of its own, registered by Init; nGZGraphic4's cDraw2D factory
// by cDraw2DSystemService::Init), makes engines and drawable buffers (a cGZDrawContextAsBuffer over a new graphics-system
// buffer), and watches the memory of the buffers it is told about: the hardware caches of 2D textures (slots 11 and 12)
// and the transient buffers (slots 9 and 10). Every fifth tick, Trim unloads the caches unused for five ticks and, when
// the transient buffers hold more than the budget, commits the ones unchanged longest first (weighted by size).
//
// 100 bytes (operator new). Faces: cIGZDrawSystem +0 (vtable 0xe4eea8, 21 slots), the cRZSystemService base +4
// (cIGZSystemService 0xe4ef00, 13 slots: Init, Shutdown and OnTick are its own) and its cRZUnknownMT +8 (0xe4ef38; slot 3
// the deleting destructor 0x8c12f0 through the thunk 0x8c12ad). Made by the COM director's creator 0x8c0920 (the
// constructor 0x8bfdb0 inline) only. Standalone shape (docs/standalone-shape.md): the creator and the constructor are
// hooked, so every draw system carries our vtables and the slots are in effect through their callers.
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include "gz/cIGZBuffer.h"
#include "gz/cIGZDrawContextEngine.h"
#include "rz/RZRefCountBase.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"

// The engine factory Init registers (12 bytes: cIGZDrawContextEngineFactory +0, vtable 0xe4ef54; cRZRefCount +4, vtable
// 0xe4ef68, count +8). Engines for targets that are cIGZBuffers: cGZDrawContextRenderToBuffer (0xe4 bytes, constructor
// 0x8c7dc0; ours, src/gz/cGZDrawContextRenderToBuffer.h). Made only by cGZDrawSystem::Init (inline), so its slots are in effect through that caller.
class cGZDrawContextRenderToBufferFactory : public cIGZDrawContextEngineFactory, public rz::cRZRefCount {
public:
    static constexpr uint32_t kSize = 0xc;

    // cIGZUnknown (the +4 face's Release / AddRef are the compiler's thunks 0x8c1338 / 0x8c1310)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8c0a20: ids 1 and kIid
    uint32_t AddRef() override;                              // 0x8c0a00 (the count's 0x148fb0)
    uint32_t Release() override;                             // 0x8c0a10 (the count's 0x1b94af)
    // The deleting destructor 0x8c12c0 (slot 0 of the +4 face, through the thunk 0x8c12a5).
    ~cGZDrawContextRenderToBufferFactory() override = default;

    bool CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine) override; // 0x8c0a50
};
static_assert(sizeof(cGZDrawContextRenderToBufferFactory) == cGZDrawContextRenderToBufferFactory::kSize,
              "cGZDrawContextRenderToBufferFactory: 12 bytes");

class cGZDrawSystem : public cIGZDrawSystem, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 100;
    static constexpr int32_t kPriority = 1500000;
    static constexpr uint32_t kDefaultBudget = 1000000;
    static constexpr uint32_t kTrimInterval = 5; // OnTick trims every 5th tick, buffers unused for 5 ticks

    cGZDrawSystem();           // 0x8bfdb0
    ~cGZDrawSystem() override; // 0x8bfe70; deleting 0x8c12f0
    // 0x8c0920, the COM director's creator (the constructor inline): a new service, count 0.
    static cGZDrawSystem *Create();

    // cIGZUnknown (all three faces; +4 and +8 through the compiler's thunks 0x8c1328 0x8c1318 0x8c1340, 0x8c1330 0x8c1320
    // 0x8c1348)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8bffb0: kIid -> this, else the service base's
    uint32_t AddRef() override;                              // 0x8bff90 (the base's)
    uint32_t Release() override;                             // 0x8bffa0 (the base's)

    // cIGZSystemService (+4)
    bool Init() override;                    // 0x8bffe0: the buffer engine factory registered, on the framework's tick
    bool Shutdown() override;                // 0x8c0060: off the tick
    bool OnTick(uint32_t counter) override;  // 0x8c08f0: Trim(5) every 5th tick, then the tick count

    // cIGZDrawSystem
    void AddFactory(cIGZDrawContextEngineFactory *factory) override;                     // 0x8c0080
    bool CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine) override;      // 0x8c00f0
    bool CreateBuffer(cIGZBuffer **out, uint32_t width, uint32_t height, uint32_t format) override; // 0x8c0130
    bool ConvertPixels(uint8_t *to, uint32_t toStride, uint32_t toFormat, const uint8_t *from, uint32_t fromStride,
                       uint32_t fromFormat, int32_t width, int32_t height, const uint32_t *key) override; // 0x8c0280
    void SetMemoryBudget(uint32_t bytes) override;                                       // 0x8c02c0
    uint32_t GetMemoryBudget() override;                                                 // 0x8c02d0
    void AddTransientBuffer(cIGZTransientBuffer *buffer) override;                       // 0x8c02e0
    void RemoveTransientBuffer(cIGZTransientBuffer *buffer) override;                    // 0x8c0380
    void AddHardwareCache(cIGZBufferHardwareCache *cache) override;                      // 0x8c03e0
    void RemoveHardwareCache(cIGZBufferHardwareCache *cache) override;                   // 0x8c0430
    void Trim(uint32_t minimumAge) override;                                             // 0x8c0470
    void FlushAll() override;                                                            // 0x8c06c0
    uint32_t GetTick() override;                                                         // 0x8c0720
    void GetHardwareCacheUsage(uint32_t *count, uint32_t *bytes) override;               // 0x8c0730
    void GetTransientBufferUsage(uint32_t *count, uint32_t *ready, uint32_t *bytes) override; // 0x8c07e0
    bool vf18(uint32_t, uint32_t) override;                                                               // 0x8c08c0: false
    void SetFlag(bool value) override;                                                   // 0x8c08d0
    bool GetFlag() override;                                                             // 0x8c08e0

    // A counted factory pointer (the original's cRZAutoRefCount, a std::list element).
    struct FactoryRef {
        explicit FactoryRef(cIGZDrawContextEngineFactory *factory) : p(factory)
        {
            if (p) p->AddRef();
        }
        FactoryRef(const FactoryRef &other) : FactoryRef(other.p) {}
        FactoryRef &operator=(const FactoryRef &) = delete;
        ~FactoryRef()
        {
            if (p) p->Release();
        }
        cIGZDrawContextEngineFactory *p;
    };

    // A transient buffer being watched: its lock total when last seen, the tick it last changed at, and Trim's weight
    // (the square root of its idle ticks times its bytes).
    struct WatchedBuffer {
        cIGZTransientBuffer *buffer;
        uint32_t lockTotal;
        uint32_t changedTick;
        float weight;
    };

    // The lock as a member (the game's cRZCriticalSection, constructed and destroyed with the service).
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    std::list<FactoryRef> mFactories;                 // +0x20 (head, size)
    OwnedLock mLock;                                  // +0x28 the caches and buffers
    std::vector<cIGZBufferHardwareCache *> mCaches;   // +0x30 (uncounted)
    uint32_t mTick;                                   // +0x3c OnTick's count
    std::vector<WatchedBuffer> mWatched;              // +0x40 (uncounted)
    std::vector<WatchedBuffer *> mTrimList;           // +0x4c Trim's candidates (empty between calls)
    uint32_t mUnused58;                               // +0x58 cleared by the constructor, read by nothing
    uint32_t mBudget;                                 // +0x5c the transient buffers' bytes Trim keeps under
    bool mFlag;                                       // +0x60 (true at first; cDraw2D reads it, slot 20)
};
static_assert(sizeof(cGZDrawSystem) == cGZDrawSystem::kSize, "cGZDrawSystem: 100 bytes");
static_assert(offsetof(cGZDrawSystem, mFactories) == 0x20 && offsetof(cGZDrawSystem, mLock) == 0x28 &&
                  offsetof(cGZDrawSystem, mCaches) == 0x30 && offsetof(cGZDrawSystem, mTick) == 0x3c &&
                  offsetof(cGZDrawSystem, mWatched) == 0x40 && offsetof(cGZDrawSystem, mTrimList) == 0x4c &&
                  offsetof(cGZDrawSystem, mBudget) == 0x5c && offsetof(cGZDrawSystem, mFlag) == 0x60,
              "cGZDrawSystem layout");

// The hooks' adapter: the constructor 0x8bfdb0 (ecx = the memory).
struct cGZDrawSystemAdapters {
    static cGZDrawSystem *__fastcall Construct(void *memory, void *);
};
