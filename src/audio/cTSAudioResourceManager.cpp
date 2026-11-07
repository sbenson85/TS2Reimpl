// cTSAudioResourceManager (lane IY): the whole class in the standalone shape (src/audio/cTSAudioResourceManager.h).
#include "audio/cTSAudioResourceManager.h"

#include "app/ReportFinish.h" // cRZVersion
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZPropertySet.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZSystemService.h"
#include "misc/UtilitiesC.h" // KeyMapSlot5Walker
#include "rz/MiscHelpersA.h" // cRZStd
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cGZAllocatorService.h" // FreeThroughAllocatorService
#include "rz/cRZStopwatch.h"

#include <cstring>
#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The resource ids: instance | resource << 32.
uint64_t IdOf(uint32_t instance, uint32_t resource) { return uint64_t(resource) << 32 | instance; }
uint64_t IdOf(const cGZPersistResourceKey *key) { return IdOf(key->instance, key->resource); }

// Original callees nobody has replaced: the sound renderer probe (cached at 0x10f51b4), BuildResourceMaps, and the
// constructors of the memory record (cTSAudioResourceMemorySerialRecord, 0x50 bytes), of the two built-in decoders (0xfdc
// and 0x38 bytes) and of the memory buffer (0x18 bytes) with its fill from a stream.
void *SoundProbe() { return OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()(); }
void BuildResourceMaps(void *manager) { OriginalFunction<void(__thiscall *)(void *), 0x22cf55>()(manager); }
cIGZUnknown *NewMemoryRecord(cIGZUnknown *buffer)
{
    void *const memory = ::operator new(0x50);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *, cIGZUnknown *), 0x22b41e>()(memory, buffer) : nullptr;
}
cIGZUnknown *NewDecoderA()
{
    void *const memory = ::operator new(0xfdc);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0xb22549>()(memory) : nullptr;
}
cIGZUnknown *NewDecoderB()
{
    void *const memory = ::operator new(0x38);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0xb22c42>()(memory) : nullptr;
}
cIGZUnknown *NewMemoryBuffer()
{
    void *const memory = ::operator new(0x18);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0x23b4a8>()(memory) : nullptr;
}
void FillMemoryBuffer(cIGZUnknown *buffer, cIGZUnknown *stream)
{
    OriginalFunction<void(__thiscall *)(void *, cIGZUnknown *), 0x21b2e2>()(buffer, stream);
}

// The cached resource manager (0x9c5b26).
void *ResourceManager() { return CachedServiceGetter<0x10f51a8, 0x056b906e, 0x656b8efc>(); }

// The framework service cached at 0x10f5194 (0x211051: filled from GetServiceObject the first time).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

// A log line: a cRZStd for (mode 8, the source file's name, line), then its slot 2 (cdecl) with the log channel, the level,
// the format and the arguments.
constexpr uint32_t kSourceFile = 0xdaae38;
template <class... A> void Log(uint32_t line, uint32_t level, uint32_t format, A... args)
{
    cRZStd log;
    cRZStd *const object = log.Construct(8, Word(GameAddress<char>(kSourceFile)), line);
    VirtualSlot<void(__cdecl *)(cRZStd *, uint32_t, uint32_t, const char *, ...)>(object, 2)(
        object, *GameAddress<uint32_t>(0xff5f10), level, GameAddress<char>(format), args...);
}

cIGZString *AsString(AudioResourceString &text) { return reinterpret_cast<cIGZString *>(&text.s); }

// The sound renderer's messages: framework class 0xcbc79579 (iid 0x0bc7957d), type 0x4e078a57 (slot 5), the payload
// object (slot 6) taking the segment (its slot 33); posted through the renderer's slot 8.
constexpr uint32_t kMessageClass = 0xcbc79579;
constexpr uint32_t kMessageIid = 0x0bc7957d;
constexpr uint32_t kSegmentAdded = 0x4e078a57;
constexpr uint32_t kSegmentMessage = 0x2df90c05;

// The renderer's events.
constexpr uint32_t kStartPreloading = 0xdfd2843;
constexpr uint32_t kBuildMaps = 0xace5f9ea;
constexpr uint32_t kShutdownEvent = 0xace5f9f1;

// The stream interface the decoders read (0xc56b8f08) and the record's namer (0x856c8cf7).
constexpr uint32_t kStreamIid = 0xc56b8f08;
constexpr uint32_t kNamerIid = 0x856c8cf7;

} // namespace

// ---- cTSAudioKeyFilterByTypesAndGroups ----

// 0x22b50b: the group set, then the type set.
cTSAudioKeyFilterByTypesAndGroups::~cTSAudioKeyFilterByTypesAndGroups() = default;

// Folded 0x99b760: 0x8865cd89 or 1 -> this, counted through slot 1.
bool cTSAudioKeyFilterByTypesAndGroups::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kInterfaceId && riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSAudioKeyFilterByTypesAndGroups::AddRef() { return ++mRefCount; }

// Folded 0x723ae0: the last reference deletes (slot 4) without counting down, and answers 0.
uint32_t cTSAudioKeyFilterByTypesAndGroups::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// 0x22df58: the key's type in the type set and its group in the group set.
bool cTSAudioKeyFilterByTypesAndGroups::IsKeyIncluded(const uint32_t *key)
{
    return mTypes.find(key[0]) != mTypes.end() && mGroups.find(key[1]) != mGroups.end();
}

// ---- construction ----

// 0x22b1d9 (581 B): the service base (id 0x6d92870d, priority -0x2dc6c0), the members in order (both flags, +0x5c, the
// counters and the preload state zero), a new cGZPropertySetString (zeroed, then constructed) held at +0x1a8, then the
// message id 0x2df90c05. +0x32, +0x33 and +0x5d..+0x5f are not written.
cTSAudioResourceManager::cTSAudioResourceManager() : rz::cRZSystemService(kServiceId, kPriority)
{
    mInitialized = 0;
    mPreloadStarted = 0;
    mByte5C = 0;
    mPreloadState = 0;
    mSoundDefinitionsLoaded = 0;
    mHitListsLoaded = 0;
    mDataSourcesLoaded = 0;
    void *const memory = ::operator new(sizeof(::cGZPropertySetString));
    cIGZUnknown *properties = nullptr;
    if (memory) {
        std::memset(memory, 0, sizeof(::cGZPropertySetString));
        properties = static_cast<cIGZPropertySetString *>(::cGZPropertySetStringAdapters::Construct(memory, nullptr));
    }
    mPropertySet.Assign(properties);
    mMessageIds.insert(kSegmentMessage);
}

// 0x22b528: the members in reverse order (the property set first, the persist system last).
cTSAudioResourceManager::~cTSAudioResourceManager() noexcept = default;

// 0x22c83b.
cTSAudioResourceManager *__cdecl cTSAudioResourceManager::Create() { return new cTSAudioResourceManager; }

cTSAudioResourceManager *__fastcall cTSAudioResourceManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioResourceManager;
}

// ---- cIGZUnknown ----

// 0x22e529: cIGZMessageTarget2 -> +0xc, cITSAudioResourceManager -> this (counted through the primary face's AddRef), else
// the service's QueryInterface.
bool cTSAudioResourceManager::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    if (riid == kMessageTargetIid) face = static_cast<cIGZMessageTarget2 *>(this);
    else if (riid == kIid) face = static_cast<cITSAudioResourceManager *>(this);
    else return rz::cRZSystemService::QueryInterface(riid, out);
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSAudioResourceManager::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSAudioResourceManager::Release() { return rz::cRZSystemService::Release(); }

// ---- cITSAudioResourceManager ----

// 0x22cd5a (292 B): registers the service with the framework, takes the persist system (framework slot 5), registers the
// audio types with its resource manager, hands the event handler and the update target (period 0x14) to the sound
// renderer, posts the build-maps event and registers the message ids. False without a renderer or a persist system (the
// framework registration stays).
bool cTSAudioResourceManager::Init()
{
    if (mInitialized) return true;
    if (void *const framework = CachedFramework()) As<ifaces::cIGZFrameWork>(framework)->AddSystemService(As<ifaces::cIGZSystemService>(static_cast<cIGZSystemService *>(this)));
    void *const probe = SoundProbe();
    if (!probe) return false;
    AudioRef persist;
    if (void *const service = GetServiceObject()) RawSlot<5, bool>(service, 0x056b906eu, 0x9af5e924u, persist.AsPPVoid());
    if (!persist) return false;
    mPersist.Assign(persist.Get());
    RawSlot<21, void>(RawSlot<3, void *>(mPersist.Get()), 0x5b945038u);
    RawSlot<20, void>(RawSlot<3, void *>(mPersist.Get()), 0xeea60a40u, 0x7b1acfcdu, 0u);
    RawSlot<20, void>(RawSlot<3, void *>(mPersist.Get()), 0xeea60a40u, 0x0b9eb87eu, 0u);
    RawSlot<15, void>(probe, static_cast<cIGZSndEventHandler *>(this));
    RawSlot<30, void>(probe, static_cast<cIGZSndUpdateTarget *>(this), 0x14u);
    RawSlot<7, void>(probe, kBuildMaps, 0u, 0u, 0u, 0u);
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
    mInitialized = 1;
    return true;
}

// 0x22e6e2 (162 B).
bool cTSAudioResourceManager::Shutdown()
{
    if (!mInitialized) return true;
    mInitialized = 0;
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
    if (void *const framework = CachedFramework()) As<ifaces::cIGZFrameWork>(framework)->RemoveSystemService(As<ifaces::cIGZSystemService>(static_cast<cIGZSystemService *>(this)));
    void *const probe = SoundProbe();
    if (!probe) return false;
    RawSlot<31, void>(probe, static_cast<cIGZSndUpdateTarget *>(this));
    RawSlot<7, void>(probe, kShutdownEvent, 0u, 0u, 0u, 0u);
    RawSlot<16, void>(probe, static_cast<cIGZSndEventHandler *>(this));
    mSegments.clear();
    mPersist.Assign(nullptr);
    return true;
}

// 0x22e78c: posts the start-preloading event to the renderer (its slot 11) once.
bool cTSAudioResourceManager::StartPreloading()
{
    if (!mInitialized) return false;
    if (mPreloadStarted) return true;
    void *const probe = SoundProbe();
    if (!probe) return false;
    RawSlot<11, void>(probe, kStartPreloading, 0u, 0u, 0u, 0u);
    mPreloadStarted = 1;
    return true;
}

// 0x22e7c4.
bool cTSAudioResourceManager::ClearInitialized()
{
    if (!mInitialized) return false;
    mInitialized = 0;
    return true;
}

// 0x22ba3a (216 B): a segment-added message for `segment` (its interface 1) posted to the renderer.
void cTSAudioResourceManager::PostSegmentAdded(cIGZUnknown *segment)
{
    if (!SoundProbe() || !segment) return;
    AudioRef message;
    void *const framework = GetDirectorFrameworkObject();
    RawSlot<3, bool>(framework, kMessageClass, kMessageIid, message.AsPPVoid());
    if (!message) return;
    AudioRef unknown;
    segment->QueryInterface(1u, unknown.AsPPVoid());
    RawSlot<5, void>(message.Get(), kSegmentAdded);
    RawSlot<33, void>(RawSlot<6, void *>(message.Get()), unknown.Get());
    RawSlot<8, void>(SoundProbe(), message.Get());
}

// 0x22ca32 / 0x22c8c4: the cache's resource by id as `iid`; else, with a zero resource word, the first key of the map
// (in its list order) with the id's instance gives it; the key's resource from the persist system's resource manager.
bool cTSAudioResourceManager::GetCached(AudioCache &cache, AudioKeyMap &keys, uint64_t id, uint32_t iid, void **out,
                                        uint32_t missingLine, uint32_t missingFormat, uint32_t failedLine, uint32_t failedFormat)
{
    if (!mPreloadStarted) StartPreloading();
    const auto hit = cache.find(id);
    if (hit != cache.end() && RawSlot<0, bool>(hit->second.Get(), iid, out)) return true;
    const uint32_t instance = static_cast<uint32_t>(id);
    uint32_t resource = static_cast<uint32_t>(id >> 32);
    if (!resource) {
        for (const auto &entry : keys)
            if (entry.second.instance == instance) {
                resource = entry.second.resource;
                break;
            }
    }
    const auto found = keys.find(IdOf(instance, resource));
    if (found == keys.end()) {
        Log(missingLine, 5, missingFormat, instance, resource);
        return false;
    }
    const cGZPersistResourceKey *const key = &found->second;
    // ORIGINAL-BUG: mPersist is used unchecked; it is null before Init and after Shutdown, which keep the key maps.
    void *const resources = RawSlot<3, void *>(mPersist.Get());
    if (RawSlot<3, bool>(resources, key, iid, out, 0u, 0u)) return true;
    Log(failedLine, 5, failedFormat, key->type, key->group, key->instance);
    return false;
}

bool cTSAudioResourceManager::GetSoundDefinition(uint64_t id, uint32_t iid, void **out)
{
    return GetCached(mSoundDefinitions, mSoundDefinitionKeys, id, iid, out, 0x149, 0xdaaed0, 0x144, 0xdaae80);
}

bool cTSAudioResourceManager::GetHitList(uint64_t id, uint32_t iid, void **out)
{
    return GetCached(mHitLists, mHitListKeys, id, iid, out, 0x186, 0xdaaf68, 0x181, 0xdaaf20);
}

// 0x22bd57 (771 B): a cached data source as a memory record's stream; else the key's record from the persist system's
// resource manager (slot 34), its stream (slot 22), as the stream interface; each through CreateDecoder (slot 11).
bool cTSAudioResourceManager::OpenDecoder(uint64_t id, void **out)
{
    const uint32_t instance = static_cast<uint32_t>(id);
    uint32_t resource = static_cast<uint32_t>(id >> 32);
    const auto hit = mDataSources.find(id);
    if (hit != mDataSources.end()) {
        AudioRef buffer(hit->second.Get());
        cIGZUnknown *const made = NewMemoryRecord(buffer.Get());
        AudioRef record(made);
        AudioRef stream;
        RawSlot<0, bool>(record.Get(), kStreamIid, stream.AsPPVoid());
        return CreateDecoder(static_cast<cIGZUnknown *>(stream.Get()), out);
    }
    if (!resource) {
        for (const auto &entry : mDataSourceKeys)
            if (entry.second.instance == instance) {
                resource = entry.second.resource;
                break;
            }
    }
    const auto found = mDataSourceKeys.find(IdOf(instance, resource));
    if (found == mDataSourceKeys.end()) {
        Log(0x1cc, 5, 0xdab004, instance, resource);
        return false;
    }
    const cGZPersistResourceKey *const key = &found->second;
    AudioRef record;
    // ORIGINAL-BUG: mPersist used unchecked, as in GetCached (0x22bec2).
    void *const resources = RawSlot<3, void *>(mPersist.Get());
    if (RawSlot<34, bool>(resources, key, record.AsPPVoid())) {
        AudioRef data;
        if (!RawSlot<22, bool>(record.Get(), key, data.AsPPVoid(), 1u)) {
            Log(0x1bb, 5, 0xdaafac, key->type, key->group, key->instance);
            return false;
        }
        {
            AudioRef stream;
            if (RawSlot<0, bool>(data.Get(), kStreamIid, stream.AsPPVoid())) return CreateDecoder(stream.Get(), out);
            RawSlot<6, void>(data.Get());
        }
    }
    Log(0x1c8, 5, 0xdaafd8, key->type, key->group, key->instance);
    return false;
}

// 0x22c05a (430 B): the first decoder that opens the stream (slot 3 takes it, slot 13 opens; a failed one is given a null
// stream): the framework's class 0x6d4df5bf, the two built-in decoders, the framework's class 0x3cec2b47. The stream is
// rewound (its slot 55) before each.
bool cTSAudioResourceManager::CreateDecoder(cIGZUnknown *stream, void **out)
{
    AudioRef decoder;
    const auto open = [&]() {
        RawSlot<3, void>(decoder.Get(), stream);
        if (RawSlot<13, bool>(decoder.Get())) return true;
        RawSlot<3, void>(decoder.Get(), nullptr);
        return false;
    };
    bool made = false;
    {
        void *const framework = GetDirectorFrameworkObject();
        void **const slot = decoder.AsPPVoid();
        if (RawSlot<3, bool>(framework, 0x6d4df5bfu, 0x3afd825fu, slot)) {
            RawSlot<55, void>(stream, 0u);
            made = open();
        }
    }
    if (!made) {
        RawSlot<55, void>(stream, 0u);
        decoder.Assign(NewDecoderA());
        made = open();
    }
    if (!made) {
        RawSlot<55, void>(stream, 0u);
        decoder.Assign(NewDecoderB());
        made = open();
    }
    if (!made) {
        void *const framework = GetDirectorFrameworkObject();
        void **const slot = decoder.AsPPVoid();
        if (!RawSlot<3, bool>(framework, 0x3cec2b47u, 0x3afd825fu, slot)) return false;
        RawSlot<55, void>(stream, 0u);
        made = open();
    }
    if (!made) return false;
    *out = decoder.Get();
    decoder->AddRef();
    return true;
}

// ---- cIGZSndEventHandler ----

// 0x22c64e (243 B): start preloading (the preload state 1 with 384 MB of memory or more, else 0); segment added (the
// payload's slot 32, as the segment interface 0x656b8f0c: its keys); build the maps; shut down.
bool cTSAudioResourceManager::HandleEvent(cIGZUnknown *event)
{
    if (!event) return false;
    switch (RawSlot<4>(event)) {
    case kStartPreloading: {
        alignas(appbase::cRZVersion) uint8_t storage[sizeof(appbase::cRZVersion)];
        auto *const version = reinterpret_cast<appbase::cRZVersion *>(storage);
        version->Construct();
        uint32_t total = 0, available = 0;
        version->GetMemory(&total, &available);
        mPreloadState = total >= 0x180 ? 1 : 0;
        version->Destruct();
        return true;
    }
    case kSegmentAdded: {
        void *const segment = RawSlot<32, void *>(RawSlot<6, void *>(event));
        if (segment) {
            AudioRef keys;
            if (static_cast<cIGZUnknown *>(segment)->QueryInterface(0x656b8f0cu, keys.AsPPVoid())) AddSegmentKeys(keys.Get());
        }
        return true;
    }
    case kBuildMaps:
        BuildResourceMaps(static_cast<cITSAudioResourceManager *>(this));
        return true;
    case kShutdownEvent:
        return true;
    default:
        return false;
    }
}

// ---- cIGZSndUpdateTarget ----

// 0x22e8a3 (190 B): the preloader's states (1 begin, 3..8 the passes, 2 end).
void cTSAudioResourceManager::Update(uint32_t)
{
    switch (mPreloadState) {
    case 1:
        Log(0x274, 5, 0xdab030); // "Begin idle preloading."
        mPreloadState = 3;
        break;
    case 2:
        Log(0x279, 5, 0xdab048); // "End idle preloading."
        mPreloadState = 0;
        break;
    case 3: // 0x22bb4c
        mPreloadCursor = mSoundDefinitionPreload.begin();
        mSoundDefinitionsLoaded = 0;
        mPreloadState = 4;
        break;
    case 4: PreloadPass(mSoundDefinitionPreload, 5, 0); break; // 0x22bcba
    case 5: // 0x22bb2f
        mPreloadCursor = mHitListPreload.begin();
        mHitListsLoaded = 0;
        mPreloadState = 6;
        break;
    case 6: PreloadPass(mHitListPreload, 7, 1); break; // 0x22bc1d
    case 7: // 0x22bb12
        mPreloadCursor = mDataSourcePreload.begin();
        mDataSourcesLoaded = 0;
        mPreloadState = 8;
        break;
    case 8: PreloadPass(mDataSourcePreload, 2, 2); break; // 0x22bb70
    default: break;
    }
}

// 0x22bcba / 0x22bc1d / 0x22bb70: from the cursor, while the stopwatch (mode 3) says under 4000, each key preloaded (the
// data sources only in an allowed group); at the list's end the list is cleared and the next state set.
void cTSAudioResourceManager::PreloadPass(AudioKeyList &list, uint32_t nextState, int kind)
{
    cRZStopwatch watch;
    watch.Construct(3);
    watch.Start();
    while (watch.Elapsed() < 4000 && mPreloadCursor != list.end()) {
        const cGZPersistResourceKey *const key = &*mPreloadCursor;
        if (kind == 0) PreloadSoundDefinition(key);
        else if (kind == 1) PreloadHitList(key);
        else if (IsPreloadGroup(key)) PreloadDataSource(key);
        ++mPreloadCursor;
    }
    if (mPreloadCursor == list.end()) {
        list.clear();
        mPreloadState = nextState;
    }
}

// ---- cIGZMessageTarget2 ----

// 0x22c741 (250 B): the segment message (0x2df90c05, as cIGZMessage2Standard) becomes a segment-added message to the
// renderer with the message's slot 32 object.
bool cTSAudioResourceManager::DoMessage(void *message)
{
    AudioRef standard;
    if (!As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(0x65297976u, standard.AsPPVoid()) || As<ifaces::cIGZMessage2Standard>(standard.Get())->GetType() != kSegmentMessage) return false;
    if (SoundProbe()) {
        AudioRef posted;
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, bool>(framework, kMessageClass, kMessageIid, posted.AsPPVoid());
        if (posted) {
            RawSlot<5, void>(posted.Get(), kSegmentAdded);
            void *const payload = RawSlot<6, void *>(posted.Get());
            void *const segment = RawSlot<32, void *>(standard.Get());
            RawSlot<33, void>(payload, segment);
            RawSlot<8, void>(SoundProbe(), posted.Get());
        }
    }
    return true;
}

// ---- members ----

// 0x22b97b (191 B): the segment's keys (slot 19, into an allocator-service vector) registered as data sources, hit lists
// and sound definitions through the three filters.
void cTSAudioResourceManager::AddSegmentKeys(cIGZUnknown *segment)
{
    if (!segment) return;
    struct {
        cGZPersistResourceKey *first, *last, *end;
    } keys{};
    As<ifaces::cIGZPersistDBSegment>(segment)->GetResourceKeyList(&keys, nullptr);
    RegisterKeys(keys.first, keys.last, &mDataSourceFilter, &mDataSourceKeys, &mDataSourcePreload);
    RegisterKeys(keys.first, keys.last, &mHitListFilter, &mHitListKeys, &mHitListPreload);
    RegisterKeys(keys.first, keys.last, &mSoundDefinitionFilter, &mSoundDefinitionKeys, &mSoundDefinitionPreload);
    if (keys.first) FreeThroughAllocatorService(keys.first, static_cast<uint32_t>(keys.end - keys.first));
}

// 0x22b7fc (383 B).
void cTSAudioResourceManager::RegisterKeys(const cGZPersistResourceKey *first, const cGZPersistResourceKey *last,
                                           cIGZPersistResourceKeyFilter *filter, AudioKeyMap *keys, AudioKeyList *preload)
{
    for (const cGZPersistResourceKey *key = first; key != last; ++key) {
        if (!filter->IsKeyIncluded(&key->type)) continue;
        const uint64_t id = IdOf(key);
        const auto found = keys->find(id);
        if (found == keys->end()) {
            keys->try_emplace(id).first->second = *key;
            preload->push_back(*key);
            continue;
        }
        AudioResourceString name, existing;
        if (GetKeyName(key, AsString(name)) && GetKeyName(&found->second, AsString(existing)) &&
            !(name.s.mString == existing.s.mString))
            Log(0x438, 2, 0xdab1b4, name.s.ToChar(), existing.s.ToChar());
    }
}

// 0x22e7d4 (207 B): the key's name from its record's namer (slot 17); without a record or a namer the key as
// "%08x:%08x:%08x" (the string's slot 31) and false.
bool cTSAudioResourceManager::GetKeyName(const cGZPersistResourceKey *key, cIGZString *out)
{
    AudioRef record;
    void *const resources = ResourceManager();
    if (As<ifaces::cIGZPersistResourceManager>(resources)->FindSegmentWithKey(key, record.AsPPVoid())) {
        AudioRef namer;
        if (RawSlot<0, bool>(record.Get(), kNamerIid, namer.AsPPVoid())) return RawSlot<17, bool>(namer.Get(), key, out);
    }
    VirtualSlot<void(__cdecl *)(cIGZString *, const char *, ...)>(out, 31)(out, GameAddress<char>(0xdab1e4), key->type,
                                                                          key->group, key->instance);
    return false;
}

// 0x22e38c / 0x22e1ef: a key not cached yet loaded (interface 1) and cached; a cached or missing one logged by name.
bool cTSAudioResourceManager::Preload(AudioCache &cache, uint32_t &loaded, const cGZPersistResourceKey *key, uint32_t cachedLine,
                                      uint32_t failedLine)
{
    const uint64_t id = IdOf(key);
    const auto logName = [&](uint32_t line, uint32_t format) {
        AudioResourceString name;
        GetKeyName(key, AsString(name));
        Log(line, 2, format, name.s.ToChar());
    };
    if (cache.find(id) != cache.end()) {
        logName(cachedLine, 0xdab23c);
        return false;
    }
    AudioRef resource;
    void *const resources = ResourceManager();
    if (!As<ifaces::cIGZPersistResourceManager>(resources)->GetResource(key, 1u, resource.AsPPVoid(), 0, 0u)) {
        logName(failedLine, 0xdab270);
        return false;
    }
    cache[id].Assign(resource.Get());
    ++loaded;
    return true;
}

bool cTSAudioResourceManager::PreloadSoundDefinition(const cGZPersistResourceKey *key)
{
    return Preload(mSoundDefinitions, mSoundDefinitionsLoaded, key, 0x4bb, 0x4c4);
}

bool cTSAudioResourceManager::PreloadHitList(const cGZPersistResourceKey *key)
{
    return Preload(mHitLists, mHitListsLoaded, key, 0x4f4, 0x4fd);
}

// 0x22df9d (594 B): the key's data read whole into a new memory buffer (its interface 1 cached).
bool cTSAudioResourceManager::PreloadDataSource(const cGZPersistResourceKey *key)
{
    const uint64_t id = IdOf(key);
    if (mDataSources.find(id) != mDataSources.end()) {
        AudioResourceString name;
        GetKeyName(key, AsString(name));
        Log(0x53e, 2, 0xdab23c, name.s.ToChar());
        return false;
    }
    AudioRef record;
    void *const resources = ResourceManager();
    if (!As<ifaces::cIGZPersistResourceManager>(resources)->FindSegmentWithKey(key, record.AsPPVoid())) {
        Log(0x545, 5, 0xdab290, key->type, key->group, key->instance);
        return false;
    }
    AudioRef data;
    if (!RawSlot<22, bool>(record.Get(), key, data.AsPPVoid(), 1u)) {
        Log(0x54c, 5, 0xdaafac, key->type, key->group, key->instance);
        return false;
    }
    AudioRef buffer(NewMemoryBuffer());
    FillMemoryBuffer(buffer.Get(), data.Get());
    RawSlot<6, void>(data.Get());
    AudioRef unknown;
    RawSlot<0, bool>(buffer.Get(), 1u, unknown.AsPPVoid());
    mDataSources[id].Assign(unknown.Get());
    ++mDataSourcesLoaded;
    return true;
}

// 0x22e6ad: the key's group among the allowed groups.
bool cTSAudioResourceManager::IsPreloadGroup(const cGZPersistResourceKey *key) const
{
    for (uint32_t group : mPreloadGroups)
        if (group == key->group) return true;
    return false;
}
