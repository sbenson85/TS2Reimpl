#pragma once
// A fake resource manager for the tests of everything that loads texts through cTSString (lane AG).
//
// The string-table loader (cTSString::Load, 0x1d10d0) asks the resource manager the getter 0x9c5b26 returns (cache global 0x10f51a8)
// for the resource of its key as a cIGZString. A test installs this fake in that global, so the original loader and ours both reach
// it, and decides in a callback what each key answers (found or not, and the text). The fake records every ask and every
// call it gets, so both worlds can be compared call for call.
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace tsfake {

// What the loader asked for: the 16-byte key at cTSString+0x20 and the other arguments of GetResource.
struct Ask {
    uint32_t type, group, instance, keyWord3;
    uint32_t interfaceId;
    int32_t loadFlags;
    uint32_t reserved;
    bool operator==(const Ask &o) const
    {
        return type == o.type && group == o.group && instance == o.instance && keyWord3 == o.keyWord3 && interfaceId == o.interfaceId &&
               loadFlags == o.loadFlags && reserved == o.reserved;
    }
};

// The calls the fakes receive, in order (the resource's Strlen before its Data is part of the contract).
enum Event : uint32_t { kEventGetResource = 1, kEventStrlen, kEventData, kEventResourceRelease, kEventServiceQuery, kEventManagerRelease };

// Where an error is thrown (a C++ exception, 0x7e57).
enum class Fault { None, GetResource, Strlen, Data };

constexpr int kFaultCode = 0x7e57;

// The answer for a key: true when the resource exists (then `text` is its text).
using Answerer = std::function<bool(const Ask &, std::string &text)>;

// How the loader finds the manager.
enum class Mode {
    Present,    // the cache global holds the fake
    ViaService, // the cache is empty; the service object (global 0x10c9378) hands the fake out
    Absent,     // the cache is empty and there is no service object: the getter returns null
};

inline std::vector<Ask> gAsks;
inline std::vector<uint32_t> gEvents;
inline Answerer gAnswerer;
inline Fault gFault = Fault::None;
inline bool gNotFoundButOutput = false; // a not-found answer still hands out a resource (it must be released)
inline bool gRecording = false;         // the asks and events are only kept for the tests that compare them (Reset turns it on)
inline int gResourcesCreated = 0;
inline int gResourcesReleased = 0;
inline uint32_t gQueriedFirstId = 0, gQueriedSecondId = 0;

struct Resource {
    const void *vtable;
    std::string text;
};

inline uint32_t __fastcall ResourceRelease(Resource *self, void *)
{
    if (gRecording) gEvents.push_back(kEventResourceRelease);
    ++gResourcesReleased;
    delete self;
    return 0;
}

inline const char *__fastcall ResourceData(Resource *self, void *)
{
    if (gRecording) gEvents.push_back(kEventData);
    if (gFault == Fault::Data) throw kFaultCode;
    return self->text.c_str();
}

inline uint32_t __fastcall ResourceStrlen(Resource *self, void *)
{
    if (gRecording) gEvents.push_back(kEventStrlen);
    if (gFault == Fault::Strlen) throw kFaultCode;
    return static_cast<uint32_t>(self->text.size());
}

inline const void *gResourceVTable[34] = {};

struct Manager {
    const void *vtable;
};

inline uint32_t __fastcall ManagerRelease(Manager *, void *)
{
    if (gRecording) gEvents.push_back(kEventManagerRelease);
    return 0;
}

inline uint32_t __fastcall ManagerGetResource(Manager *, void *, const uint32_t *key, uint32_t interfaceId, void **out, int32_t loadFlags,
                                              uint32_t reserved)
{
    if (gRecording) gEvents.push_back(kEventGetResource);
    const Ask ask{key[0], key[1], key[2], key[3], interfaceId, loadFlags, reserved};
    if (gRecording) gAsks.push_back(ask);
    if (gFault == Fault::GetResource) throw kFaultCode;
    std::string text;
    const bool found = gAnswerer ? gAnswerer(ask, text) : false;
    if (found || gNotFoundButOutput) {
        Resource *resource = new Resource{gResourceVTable, found ? text : std::string()};
        ++gResourcesCreated;
        *out = resource;
    }
    return found ? 1 : 0;
}

inline const void *gManagerVTable[34] = {};
inline Manager gManager{gManagerVTable};

struct Service {
    const void *vtable;
};

// The service object's query (slot 5): hands out the manager.
inline uint32_t __fastcall ServiceQuery(Service *, void *, uint32_t firstId, uint32_t secondId, void **out)
{
    if (gRecording) gEvents.push_back(kEventServiceQuery);
    gQueriedFirstId = firstId;
    gQueriedSecondId = secondId;
    *out = &gManager;
    return 0;
}

inline const void *gServiceVTable[34] = {};
inline Service gService{gServiceVTable};

// Installs the fake in the game image for the life of the object. Reset(mode) clears the log and sets the globals for a run.
class ManagerFixture {
public:
    explicit ManagerFixture(difftest::Context &ctx)
        : mCache(ctx.image.At<void *>(0x10f51a8)), mService(ctx.image.At<void *>(0x10c9378)), mSavedCache(*mCache), mSavedService(*mService)
    {
        // The original loader replaces bullets with two function-local statics (a needle at 0x10cc044 and a replacement at 0x10cc068,
        // guards 0x10cc064 and 0x10cc088). The harness does not run the game's static initialisation, so the fixture builds them
        // (the real strings) and makes the guards read as initialised: the lowest epoch, which no thread's epoch is below.
        mGuards[0] = *ctx.image.At<int>(0x10cc064);
        mGuards[1] = *ctx.image.At<int>(0x10cc088);
        std::memcpy(mNeedle, ctx.image.At<uint8_t>(0x10cc044), sizeof(mNeedle));
        std::memcpy(mReplacement, ctx.image.At<uint8_t>(0x10cc068), sizeof(mReplacement));
        mImage = &ctx.image;
        *ctx.image.At<uint32_t>(0x10cc064) = 0x80000000u;
        *ctx.image.At<uint32_t>(0x10cc088) = 0x80000000u;
        reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(0x10cc044))->Construct("\xe2\x80\xa2");
        reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(0x10cc068))->Construct("\xe2\x80\x8b");
        gResourceVTable[2] = reinterpret_cast<const void *>(&ResourceRelease);
        gResourceVTable[6] = reinterpret_cast<const void *>(&ResourceData);
        gResourceVTable[7] = reinterpret_cast<const void *>(&ResourceStrlen);
        gManagerVTable[2] = reinterpret_cast<const void *>(&ManagerRelease);
        gManagerVTable[3] = reinterpret_cast<const void *>(&ManagerGetResource);
        gServiceVTable[5] = reinterpret_cast<const void *>(&ServiceQuery);
    }
    ManagerFixture(const ManagerFixture &) = delete;
    ManagerFixture &operator=(const ManagerFixture &) = delete;
    ~ManagerFixture()
    {
        reinterpret_cast<cRZString *>(mImage->At<uint8_t>(0x10cc044))->Destruct();
        reinterpret_cast<cRZString *>(mImage->At<uint8_t>(0x10cc068))->Destruct();
        std::memcpy(mImage->At<uint8_t>(0x10cc044), mNeedle, sizeof(mNeedle));
        std::memcpy(mImage->At<uint8_t>(0x10cc068), mReplacement, sizeof(mReplacement));
        *mImage->At<int>(0x10cc064) = mGuards[0];
        *mImage->At<int>(0x10cc088) = mGuards[1];
        *mCache = mSavedCache;
        if (mTouchedService) *mService = mSavedService;
        gAnswerer = nullptr;
        gFault = Fault::None;
        gNotFoundButOutput = false;
        gRecording = false;
    }

    // The state before one run: no recorded asks or events, the globals as `mode` says.
    void Reset(Mode mode)
    {
        gRecording = true;
        mTouchedService = true;
        gAsks.clear();
        gEvents.clear();
        gResourcesCreated = gResourcesReleased = 0;
        gQueriedFirstId = gQueriedSecondId = 0;
        *mCache = mode == Mode::Present ? static_cast<void *>(&gManager) : nullptr;
        *mService = mode == Mode::ViaService ? static_cast<void *>(&gService) : nullptr;
    }
    void *CacheValue() const { return *mCache; }
    // For the tests of callers (they set the service object up themselves): only the cache global holds the fake; the service global
    // and the log are left alone.
    void InstallManagerOnly() { *mCache = &gManager; }

private:
    void **mCache;
    void **mService;
    void *mSavedCache;
    void *mSavedService;
    bool mTouchedService = false;
    const ::OriginalImage *mImage = nullptr;
    int mGuards[2] = {};
    uint8_t mNeedle[0x20] = {}, mReplacement[0x20] = {};
};

} // namespace tsfake
