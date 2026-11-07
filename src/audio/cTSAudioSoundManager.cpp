// cTSAudioSoundManager (lane IY): the whole class in the standalone shape (src/audio/cTSAudioSoundManager.h).
#include "audio/cTSAudioSoundManager.h"

#include "app/AppServices2.h" // AcquireTestingService
#include "audio/cTSAudioEvent.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"           // gz::AnyUint32
#include "gz/cGZPropertySet.h"   // gzany::kStringVTable
#include "gz/cIGZPropertySet.h"  // GZServiceString
#include "gz/cIGZString.h"
#include "gz/cAttributeIDIterator.h"
#include "gz/cGZEventReflector.h" // nGZEvent::MakeIteratorRange
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cIGZSystemService.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"     // KeyMapSlot5Walker
#include "misc/X87.h"
#include "rz/MiscHelpersA.h"     // cRZStd
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/Singleton_20ddfb.h" // GetServiceObject, GetCachedSingleton, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h"   // GetCachedSlot23
#include "rz/ServiceGetters.h" // CachedServiceGetter
#include "rz/StringKeyParser.h"
#include "rz/cTSIniFile.h" // cRZStringTokenizer
#include "rz/nGZLog.h" // StoreCurrentThreadId
#include "rz/RZTextParse.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <new>
#include <xmmintrin.h>
#include <vector>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A log line (cRZStd for mode 8, this file's name and the line; its slot 2 with the channel, level, format, arguments).
constexpr uint32_t kSourceFile = 0xda9560;
template <class... A> void Log(uint32_t line, uint32_t level, uint32_t format, A... args)
{
    cRZStd log;
    cRZStd *const object = log.Construct(8, Word(GameAddress<char>(kSourceFile)), line);
    VirtualSlot<void(__cdecl *)(cRZStd *, uint32_t, uint32_t, const char *, ...)>(object, 2)(
        object, *GameAddress<uint32_t>(0xff5f10), level, GameAddress<char>(format), args...);
}

// A service looked up through the framework's slot 5 (the 37-byte getters 0x20db8e, 0x212e8c, 0x20e122, 0x18ae10),
// released at scope end.
struct ServiceRef {
    void *p = nullptr;
    ServiceRef(uint32_t id, uint32_t iid)
    {
        if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &p);
    }
    ServiceRef(const ServiceRef &) = delete;
    ServiceRef &operator=(const ServiceRef &) = delete;
    ~ServiceRef()
    {
        if (p) RawSlot<2>(p);
    }
};
constexpr uint32_t kRendererId = 0x23cecba0, kRendererIid = 0x106077d2;  // 0x20db8e
constexpr uint32_t kService9CId = 0x1af85bfb, kService9CIid = 0x3af85bb7; // 0x212e8c
constexpr uint32_t kVoiceId = 0xe99b3207, kVoiceIid = 0x899b182d;         // 0x20e122
constexpr uint32_t kViewId = 0xfb6372ee, kViewIid = 0x5b637209;           // 0x18ae10

// The framework service cached at 0x10f5194 (0x211051).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

// The app's slot 31 (the testing-cheats switch).
bool CheatsEnabled() { return RawSlot<31, bool>(GetCachedSingleton()); }

// The sound registry 0x21ac34: the original's 0x21b370 (cdecl: 0 and a six-word info block naming 0xd9b9d0) returns the
// map of registered sounds (id -> six words).
using AudioRegistry = std::map<uint32_t, std::array<uint32_t, 6>>;
AudioRegistry *Registry()
{
    uint32_t info[6] = {0, Word(GameAddress<void>(0xd9b9d0)), 0, 0, 0, 0};
    return OriginalFunction<AudioRegistry *(__cdecl *)(uint32_t, uint32_t *), 0x21b370>()(0, info);
}

// The id list GetRegisteredIds hands out (cRZRefCountedT<std::vector<unsigned long>>, vtable 0xd9bcfc): counted at
// +0x10, deleted at the last Release.
class AudioIdVector : public cIGZUnknown {
public:
    std::vector<uint32_t> mIds; // +4
    uint32_t mRefs = 0;         // +0x10
    bool QueryInterface(uint32_t riid, void **out) override
    {
        if (riid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefs; }
    uint32_t Release() override
    {
        if (mRefs > 1) return --mRefs;
        delete this;
        return 0;
    }
    virtual ~AudioIdVector() = default; // slot 3
};
static_assert(sizeof(AudioIdVector) == 0x14);

// The listener range the event manager's slot 10 fills (as cTSSGSystem's).
struct ListenerRange {
    void *iterator = nullptr;
    void *second = nullptr;
    void *end = nullptr;
    ListenerRange() = default;
    ListenerRange(const ListenerRange &) = delete;
    ListenerRange &operator=(const ListenerRange &) = delete;
    ~ListenerRange()
    {
        if (end) RawSlot<2>(end);
        if (second) RawSlot<2>(second);
        if (iterator) RawSlot<2>(iterator);
    }
    bool AtEnd() const { return RawSlot<4, uint8_t>(iterator, end) != 0; }
    void *Current(uint32_t *value) const
    {
        uint32_t first[2], next[2];
        RawSlot<5>(iterator, first);
        RawSlot<5>(iterator, next);
        *value = next[1];
        return reinterpret_cast<void *>(uintptr_t(first[0]));
    }
    void Next() const { RawSlot<6>(iterator); }
};

// Event ids and keys.
constexpr uint32_t kActiveEvent = 0x5b51e806;
constexpr uint32_t kGameStateEvent = 0x1b637474;
constexpr uint32_t kEventIid = cITSAudioEvent::kIid;
constexpr uint32_t kStandardIid = 0x65297976;
constexpr uint32_t kFlagKey = 0x8bd63096;
constexpr uint32_t kInstanceKey = 0x6bd63081;
constexpr uint32_t kOutKey = 0xbd63091;

} // namespace

// ---- helpers of the class ----

AudioEmbeddedBuffer::AudioEmbeddedBuffer() { OriginalFunction<void *(__thiscall *)(void *), 0x23b4a8>()(this); }
AudioEmbeddedBuffer::~AudioEmbeddedBuffer() { OriginalFunction<void(__thiscall *)(void *), 0x23b4ce>()(this); }

// 0x21d114.
bool AudioRamp::Step(float elapsed)
{
    if (target == value) return false;
    // The step is clamped to the distance left (minss / maxss with the step first: a NaN step gives the distance; MSVC
    // swaps the operands of a plain `?:`).
    const __m128 step = _mm_set_ss(rate * elapsed);
    const __m128 diff = _mm_set_ss(target - value);
    const __m128 clamped = rate > *GameAddress<float>(0xd9e82c) ? _mm_min_ss(step, diff) : _mm_max_ss(step, diff);
    value += _mm_cvtss_f32(clamped);
    return true;
}

// ---- construction ----

// 0x2188d6 (1062 B): the members in order (see the header), then the categories: master fade {1, 1}, each category's
// volume 1, enabled, fade {1, 1} and the word at +0x74 0.
cTSAudioSoundManager::cTSAudioSoundManager() : mMasterFade{0, 0, 0}, mCategoryFades{}
{
    mMasterFade.value = 1.0f;
    mMasterFade.target = 1.0f;
    for (int i = 0; i < 9; ++i) {
        mCategoryVolumes[i] = 1.0f;
        mCategoryEnabled[i] = 1;
        mCategoryFades[i].value = 1.0f;
        mCategoryFades[i].target = 1.0f;
        mWords74[i] = 0;
    }
}

// 0x218cf9: the members in reverse order.
cTSAudioSoundManager::~cTSAudioSoundManager() = default;

// 0x21a182.
cTSAudioSoundManager *__cdecl cTSAudioSoundManager::Create() { return new cTSAudioSoundManager; }

cTSAudioSoundManager *__fastcall cTSAudioSoundManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioSoundManager;
}

// ---- cIGZUnknown ----

// 0x21b144: the faces by interface (counted through the primary), else the behaviour base's 0x1aa007.
bool cTSAudioSoundManager::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    switch (riid) {
    case 1:
    case kEventHandlerIid: face = static_cast<cIGZSndEventHandler *>(this); break;
    case cITSAudioSoundManager::kIid: face = static_cast<cITSAudioSoundManager *>(this); break;
    case cIGZSndUpdateTarget::kIid: face = static_cast<cIGZSndUpdateTarget *>(this); break;
    case cIGZSndCompletionFunctionObject::kIid: face = static_cast<cIGZSndCompletionFunctionObject *>(this); break;
    case kMessageTargetIid: face = static_cast<cIGZMessageTarget2 *>(this); break;
    case 0x287fb697: face = static_cast<cIGZSystemService *>(this); break;
    default:
        if (!out) return false;
        if (riid == 0x3b253dc7) {
            *out = static_cast<cITSAudioSoundManager *>(this);
            AddRef();
            return true;
        }
        if (riid == 0xfb859324) {
            *out = static_cast<cITSAudioSoundManager *>(this);
            return true;
        }
        return false;
    }
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSAudioSoundManager::AddRef() { return ++mRefCount & 0x7fffffff; }

// 0x21b4fb: the last reference makes the event manager forget the manager, then deletes it.
uint32_t cTSAudioSoundManager::Release()
{
    if ((mRefCount & 0x7fffffff) == 1) {
        if (GetEventManager()) RawSlot<5>(GetEventManager(), static_cast<cITSAudioSoundManager *>(this));
        delete this;
        return 0;
    }
    --mRefCount;
    return mRefCount & 0x7fffffff;
}

// ---- the behaviour type ----

// 0x2190da.
void *cTSAudioSoundManager::GetEventManager()
{
    void *&cell = *GameAddress<void *>(0x10cc528);
    if (!cell) {
        ServiceRef service(0x5b1e896f, 0x1b1e895a);
        if (service.p) cell = RawSlot<3, void *>(service.p);
    }
    return cell;
}

// 0x21a4f8: the base's active switch (0x21a4cb), then the game state's run / pause (kinds 1, 2 and 9) passed to the
// renderer as event 0x6a95cba2.
void cTSAudioSoundManager::OnEvent(uint32_t event, uint32_t, void *data, uint32_t)
{
    if (event == kActiveEvent) {
        const uint8_t active = RawSlot<6, uint8_t>(GetEventManager(), static_cast<cITSAudioSoundManager *>(this));
        if (uint8_t(mRefCount >> 31) != active) mRefCount -= 0x80000000u;
    }
    if (event != kGameStateEvent) return;
    const uint32_t kind = static_cast<const uint32_t *>(data)[1];
    uint32_t value;
    if (kind == 1)
        value = 1;
    else if (kind == 2 || kind == 9)
        value = 0;
    else
        return;
    if (!mRenderer || !mFlag2F8) return;
    cITSAudioEvent *const e = NewEvent(0x6a95cba2);
    e->SetUint32(kFlagKey, value);
    RawSlot<12>(mRenderer.Get(), e);
    e->Release();
}

bool cTSAudioSoundManager::AskListenersAlias(uint32_t kind, uint32_t a, uint32_t b) { return AskListeners(kind, a, b); }

// 0x21b5c4.
void cTSAudioSoundManager::NotifyListeners(uint32_t kind, uint32_t a, uint32_t b)
{
    if (static_cast<int32_t>(mRefCount) >= 0) return;
    ListenerRange range;
    RawSlot<10>(GetEventManager(), &range, static_cast<cITSAudioSoundManager *>(this), kind);
    while (!range.AtEnd()) {
        uint32_t value;
        void *const listener = range.Current(&value);
        RawSlot<3>(listener, value, a, b, static_cast<cITSAudioSoundManager *>(this));
        range.Next();
    }
}

// 0x219fff: every listener says yes, and there is one.
bool cTSAudioSoundManager::AskListeners(uint32_t kind, uint32_t a, uint32_t b)
{
    if (static_cast<int32_t>(mRefCount) >= 0) return false;
    bool every = true, any = false;
    {
        ListenerRange range;
        RawSlot<10>(GetEventManager(), &range, static_cast<cITSAudioSoundManager *>(this), kind);
        while (!range.AtEnd()) {
            any = true;
            uint32_t value;
            void *const listener = range.Current(&value);
            const bool yes = RawSlot<4, uint8_t>(listener, value, a, b) != 0;
            every = yes && every;
            range.Next();
        }
    }
    return every && any;
}

void cTSAudioSoundManager::EventManagerSlot3(uint32_t a, uint32_t b, uint32_t c)
{
    RawSlot<3>(GetEventManager(), static_cast<cITSAudioSoundManager *>(this), a, b, c);
}

void cTSAudioSoundManager::EventManagerSlot4(uint32_t a, uint32_t b, uint32_t c)
{
    RawSlot<4>(GetEventManager(), static_cast<cITSAudioSoundManager *>(this), a, b, c);
}

// 0x21aebf.
uint32_t cTSAudioSoundManager::GetRegisteredCount() { return static_cast<uint32_t>(Registry()->size()); }

// 0x218f78: the registry's ids into a new counted vector, handed out as a range of two iterators over it (the original's
// iterator constructor 0x1a9942 and range constructor 0x1a98e0, both ours).
void **cTSAudioSoundManager::GetRegisteredIds(void **out)
{
    AudioIdVector *const ids = new AudioIdVector;
    ids->AddRef();
    for (auto it = Registry()->begin(); it != Registry()->end(); ++it) ids->mIds.push_back(it->first);
    using nGZEvent::cAttributeIDIterator;
    void *last = ::operator new(0x10);
    if (last) {
        const uint32_t *const cursor = ids->mIds.data() + ids->mIds.size();
        last = static_cast<cAttributeIDIterator *>(last)->Construct(static_cast<cIGZUnknown *>(ids), &cursor);
    }
    void *first = ::operator new(0x10);
    if (first) {
        const uint32_t *const cursor = ids->mIds.data();
        first = static_cast<cAttributeIDIterator *>(first)->Construct(static_cast<cIGZUnknown *>(ids), &cursor);
    }
    // Ours of 0x1a98e0, through its hook's entry point (the same convention, so a test can stand in for both).
    nGZEvent::cGZEventReflectorAdapters::MakeIteratorRange(reinterpret_cast<nGZEvent::cAttributeIDRange *>(out), nullptr,
                                                           static_cast<nGZEvent::cIAttributeIDIterator *>(first),
                                                           static_cast<nGZEvent::cIAttributeIDIterator *>(last));
    ids->Release();
    return out;
}

// 0x219082: the id's six words, or the default block at 0xff45fc.
uint32_t *cTSAudioSoundManager::GetRegisteredInfo(uint32_t *out, uint32_t id)
{
    AudioRegistry *const registry = Registry();
    const auto found = registry->find(id);
    const uint32_t *from = found != Registry()->end() ? found->second.data() : GameAddress<uint32_t>(0xff45fc);
    for (int i = 0; i < 6; ++i) out[i] = from[i];
    return out;
}

// ---- the manager ----

// 0x21a649 (1002 B).
bool cTSAudioSoundManager::Init()
{
    ServiceRef renderer(kRendererId, kRendererIid);
    if (!renderer.p) return false;
    mRenderer.Assign(static_cast<cIGZUnknown *>(renderer.p));
    ServiceRef service9C(kService9CId, kService9CIid);
    if (!service9C.p) return false;
    mService9C.Assign(static_cast<cIGZUnknown *>(service9C.p));
    ServiceRef voice(kVoiceId, kVoiceIid);
    if (!voice.p) return false;
    mVoiceService.Assign(static_cast<cIGZUnknown *>(voice.p));

    AudioRef demon;
    void *const framework = GetDirectorFrameworkObject();
    RawSlot<3, bool>(framework, 0x8d5812a4u, 0x4d7fd97eu, demon.AsPPVoid());
    if (!demon || !RawSlot<3, bool>(demon.Get())) {
        Log(0x131, 2, 0xda95a4);
        return false;
    }
    mUpdateDemon.Assign(demon.Get());
    if (CheatsEnabled()) OriginalFunction<void(__thiscall *)(void *), 0x21ea18>()(this);

    AudioRef reverb;
    if (RawSlot<0, bool>(mVoiceService.Get(), 0xab79e31cu, reverb.AsPPVoid())) {
        const char *const name = GameAddress<char>(0xda95d4); // "expverbtest3"
        const uint32_t instance = StringKeyParser::Crc24(name, 0) | 0xff000000u;
        const uint32_t high = StringKeyParser::Crc32(name, 1);
        uint32_t key[4] = {0x8db5e4c2, 0xadb5e4c8, instance, high};
        if (OriginalFunction<bool(__thiscall *)(void *, uint32_t *), 0x21b203>()(&mBuffer, key)) {
            RawSlot<4>(reverb.Get(), 1u);
            auto *const data = reinterpret_cast<std::vector<char> *>(mBuffer.raw + 8);
            RawSlot<5>(reverb.Get(), 0u, &data->at(0));
            RawSlot<7>(reverb.Get(), 0u, 1.0f);
            mFlag20B = 1;
        }
    }
    LoadSoundDefaults();
    uint32_t voices;
    if (RawSlot<19, bool>(mVoiceService.Get(), 0u, &voices)) Log(0x14d, 5, 0xda95e4, voices);
    if (mRenderer) {
        RawSlot<15>(mRenderer.Get(), static_cast<cIGZSndEventHandler *>(this));
        RawSlot<30>(mRenderer.Get(), static_cast<cIGZSndUpdateTarget *>(this), 0x14u);
    }
    ServiceRef view(kViewId, kViewIid);
    if (view.p) RawSlot<8>(view.p, kGameStateEvent, static_cast<cITSAudioSoundManager *>(this), kGameStateEvent);
    for (uint32_t id : {0xcbccdeceu, 0xb6u, 0xb2u, 0xb3u, 0xb4u, 0xb5u, 0xcdafda21u, 0xece20be8u, 0xcbea26d7u, 0x4d213401u})
        mMessageIds.insert(id);
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
    cIGZSystemService *const service = this;
    As<ifaces::cIGZFrameWork>(CachedFramework())->AddSystemService(As<ifaces::cIGZSystemService>(service));
    As<ifaces::cIGZFrameWork>(CachedFramework())->AddToTick(As<ifaces::cIGZSystemService>(service));
    service->SetServiceRunning(true);
    {
        ScopedRef testing;
        as2::AcquireTestingService(&testing, nullptr);
        if (testing.mObject) {
            GZServiceString text(*GameAddress<const char *>(0xff457c));
            RawSlot<4>(testing.mObject, target, 0x6d21507bu, &text, 0u);
        }
        ApplySettings();
        testing.ReleaseObject(); // the original holds the testing service to the end
    }
    return true;
}

// 0x21c8df (520 B).
bool cTSAudioSoundManager::Shutdown()
{
    FlushPendingEvents();
    cIGZSystemService *const service = this;
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveFromTick(As<ifaces::cIGZSystemService>(service));
    As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveSystemService(As<ifaces::cIGZSystemService>(service));
    ServiceRef view(kViewId, kViewIid);
    if (view.p) RawSlot<9>(view.p, kGameStateEvent, static_cast<cITSAudioSoundManager *>(this), kGameStateEvent);
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
    if (mRenderer) {
        cITSAudioEvent *const stop = NewEvent(0xcaa44ca7);
        stop->SetUint32(kFlagKey, 1);
        stop->SetUint32(0x6d03f57a, 0xffffffffu);
        RawSlot<8>(mRenderer.Get(), stop);
        AudioRef held;
        *reinterpret_cast<void **>(&held) = stop; // the reference NewEvent took
        held.Assign(static_cast<cITSAudioEvent *>(new cTSAudioEvent(0xccdde023)));
        cIGZUnknown *const final = held.Get();
        RawSlot<12>(mRenderer.Get(), final);
        RawSlot<3>(&mFinalEventSignal.s, 100000u);
        // ORIGINAL-BUG: "mThreadFinalEventSignal was not signaled" is logged whether or not the wait was signalled.
        Log(0x1b4, 6, 0xda9610);
        RawSlot<48>(mRenderer.Get());
        RawSlot<31>(mRenderer.Get(), static_cast<cIGZSndUpdateTarget *>(this));
        RawSlot<16>(mRenderer.Get(), static_cast<cIGZSndEventHandler *>(this));
    }
    if (mUpdateDemon) {
        RawSlot<4>(mUpdateDemon.Get());
        mUpdateDemon.Assign(nullptr);
    }
    mRenderer.Assign(nullptr);
    mService9C.Assign(nullptr);
    mVoiceService.Assign(nullptr);
    mRefB0.Assign(nullptr);
    return true;
}

// ---- the thread proxies ----

bool cTSAudioSoundManager::OnRendererThread()
{
    uint32_t thread;
    nGZLog::StoreCurrentThreadId(&thread);
    return RawSlot<51>(mRenderer.Get()) == thread;
}

cITSAudioEvent *cTSAudioSoundManager::NewEvent(uint32_t id)
{
    cITSAudioEvent *const e = new cTSAudioEvent(id);
    e->AddRef();
    return e;
}

// A request for the renderer's thread: the event, filled, posted through slot 17.
template <class Fill> void cTSAudioSoundManager::Post(uint32_t id, Fill fill)
{
    cITSAudioEvent *const e = NewEvent(id);
    fill(e);
    PostEvent(e);
    e->Release();
}

// A query: the event, filled, sent through the renderer's slot 8; its answer.
template <class Fill> bool cTSAudioSoundManager::Send(uint32_t id, Fill fill)
{
    cITSAudioEvent *const e = NewEvent(id);
    fill(e);
    const bool sent = RawSlot<8, bool>(mRenderer.Get(), e);
    e->Release();
    return sent;
}

// 0x21b0a9: on the renderer's thread to it directly, else onto the pending list.
void cTSAudioSoundManager::PostEvent(cITSAudioEvent *event)
{
    if (OnRendererThread()) {
        RawSlot<12>(mRenderer.Get(), event);
        return;
    }
    AudioRef held(event);
    mPending.push_back(held);
}

void cTSAudioSoundManager::Request18(float a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, float, float), 0x220269>()(this, a, b);
    Post(0x2a5b69c0, [&](cITSAudioEvent *e) {
        e->SetFloat(0xbd63084, a);
        e->SetFloat(0x8bd63940, b);
    });
}

void cTSAudioSoundManager::Request19(uint32_t a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, float), 0x220062>()(this, a, b);
    Post(0x8bc77093, [&](cITSAudioEvent *e) {
        e->SetUint32(0x8bd63087, a);
        e->SetFloat(0xbd63084, b);
    });
}

void cTSAudioSoundManager::Request20(uint32_t a, float b, float c)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, float, float), 0x21d882>()(this, a, b, c);
    Post(0xad9f9fee, [&](cITSAudioEvent *e) {
        e->SetUint32(0x8bd63087, a);
        e->SetFloat(0xbd63084, b);
        e->SetFloat(0x8bd63940, c);
    });
}

bool cTSAudioSoundManager::Request21(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    if (!mRenderer) return false;
    if (OnRendererThread())
        return OriginalFunction<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t),
                                0x21d3ff>()(this, a, b, c, d, e, f);
    return Send(0x4bb54186, [&](cITSAudioEvent *ev) {
        ev->SetUint32(0xbd63077, a);
        ev->SetUint32(0xedd18e72, b);
        ev->SetUint32(kInstanceKey, c);
        ev->SetUint32(0xabd6308b, d);
        ev->SetUint32(0x2bd6308e, e);
        ev->SetUint32(kOutKey, f);
    });
}

bool cTSAudioSoundManager::Request22(uint32_t a, uint32_t b, uint32_t c, cIGZUnknown *object)
{
    if (!mRenderer) return false;
    if (OnRendererThread())
        return OriginalFunction<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, cIGZUnknown *), 0x21d4ff>()(
            this, a, b, c, object);
    return Send(0xec7153c5, [&](cITSAudioEvent *e) {
        e->SetUint32(0xbd63077, a);
        e->SetUint32(0xedd18e72, b);
        e->SetUint32(kInstanceKey, c);
        RawSlot<33>(e->AsMessage(), object);
    });
}

void cTSAudioSoundManager::Request23(uint32_t a, cIGZUnknown *object)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, cIGZUnknown *), 0x22094a>()(this, a, object);
    Post(0xd96318d, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        RawSlot<33>(e->AsMessage(), object);
    });
}

void cTSAudioSoundManager::Request24(uint32_t a, cIGZUnknown *object)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, cIGZUnknown *), 0x220835>()(this, a, object);
    Post(0xd963193, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        RawSlot<33>(e->AsMessage(), object);
    });
}

void cTSAudioSoundManager::Request25(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t),
                                0x220d0a>()(this, a, b, c, d, e, flag);
    Post(0xea451903, [&](cITSAudioEvent *ev) {
        ev->SetUint32(0xbd63077, a);
        ev->SetUint32(0xedd18e72, b);
        ev->SetUint32(kInstanceKey, c);
        ev->SetUint32(0xac5b5b4f, d);
        ev->SetUint32(0x8bd63087, e);
        ev->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request26(uint32_t a, uint32_t b, uint32_t c, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0x220eec>()(this, a, b,
                                                                                                                c, flag);
    Post(0x6bb5418a, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(0x8bd63087, c);
        e->SetUint32(0xac5b5b4f, b);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request27(uint32_t a, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0x221c1e>()(this, a, flag, 0u);
    Post(0x6a451908, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request28(uint32_t a, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x222000>()(this, a, flag);
    Post(0xcbb78f7a, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request29(uint32_t a, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x221b59>()(this, a, flag);
    Post(0xcbb78f7a, [&](cITSAudioEvent *e) {
        e->SetUint32(0xac5b5b4f, a);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request30(uint32_t a, uint32_t flag, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, float), 0x21f0cc>()(this, a, flag, b);
    Post(0x6c0fd27e, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(kFlagKey, uint8_t(flag));
        e->SetFloat(0x8bd63940, b);
    });
}

// Slot 31: sent through the renderer's slot 8 (its answer dropped).
void cTSAudioSoundManager::Request31(uint32_t flag, uint32_t a)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x221a18>()(this, flag, a);
    Send(0xcaa44ca7, [&](cITSAudioEvent *e) {
        e->SetUint32(kFlagKey, uint8_t(flag));
        e->SetUint32(0x6d03f57a, a);
    });
}

void cTSAudioSoundManager::Request32(uint32_t flag, uint32_t a)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x21f000>()(this, flag, a);
    Post(0xac0fd2a2, [&](cITSAudioEvent *e) {
        e->SetUint32(kFlagKey, uint8_t(flag));
        e->SetUint32(0x6d03f57a, a);
    });
}

void cTSAudioSoundManager::Request33(uint32_t a)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t), 0x21db90>()(this, a);
    Post(0x6bb54195, [&](cITSAudioEvent *e) { e->SetUint32(kInstanceKey, a); });
}

void cTSAudioSoundManager::Request34(uint32_t a, const float *position)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, const float *), 0x220af9>()(this, a, position);
    Post(0xca45190f, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetVector3(0x6bd63530, position);
    });
}

void cTSAudioSoundManager::Request35(uint32_t a, const float *position)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, const float *), 0x2208dc>()(this, a, position);
    Post(0x8c3f9e0e, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetVector3(0x8c3f9e02, position);
    });
}

void cTSAudioSoundManager::Request36(uint32_t a, float b, float c)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, float, float), 0x220b67>()(this, a, b, c);
    Post(0x4a451915, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0xbd63084, b);
        e->SetFloat(0x8bd63940, c);
    });
}

void cTSAudioSoundManager::Request37()
{
    if (!mRenderer) return;
    if (OnRendererThread()) return UpdateSounds(0);
    Post(0xabb5419e, [](cITSAudioEvent *) {});
}

// 0x21a1bd: the next instance id (0xff4af8), wrapping to 0x10001 at 0x20000000.
uint32_t cTSAudioSoundManager::NewInstanceId()
{
    uint32_t &next = *GameAddress<uint32_t>(0xff4af8);
    uint32_t id = next;
    if (id >= 0x20000000) id = 0x10000;
    next = id + 1;
    return id + 1;
}

// 0x21a252: on another thread the renderer writes the time into the local through the event's out key.
float cTSAudioSoundManager::GetSoundCompletionTime(uint32_t instance)
{
    if (!mRenderer) return 0.0f;
    if (OnRendererThread())
        return static_cast<float>(OriginalFunction<double(__thiscall *)(void *, uint32_t), 0x21dd75>()(this, instance));
    float time = 0.0f;
    cITSAudioEvent *const e = NewEvent(0x8bdf1f98);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kOutKey, Word(&time));
    float result;
    if (RawSlot<8, bool>(mRenderer.Get(), e)) {
        result = time;
    } else {
        Log(0x38e, 2, 0xda9664, instance);
        result = 0.0f;
    }
    e->Release();
    return result;
}

// 0x21a36d.
bool cTSAudioSoundManager::GetSoundEmitter(uint32_t instance, uint32_t master, uint32_t out)
{
    if (!mRenderer) return false;
    if (OnRendererThread())
        return OriginalFunction<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0x21dde7>()(this, instance, master,
                                                                                                      out);
    cITSAudioEvent *const e = NewEvent(0x4e66585c);
    e->SetUint32(kInstanceKey, instance);
    e->SetUint32(kOutKey, out);
    const uint32_t isMaster = uint8_t(master);
    e->SetUint32(kFlagKey, isMaster);
    bool found = true;
    if (!RawSlot<8, bool>(mRenderer.Get(), e)) {
        Log(0x3a9, 2, 0xda9698, instance, isMaster);
        found = false;
    }
    e->Release();
    return found;
}

// 0x21caef: the answer byte written through the out key (false when the renderer leaves it).
bool cTSAudioSoundManager::Query41(uint32_t a)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) return OriginalFunction<bool(__thiscall *)(void *, uint32_t), 0x220c98>()(this, a);
    uint8_t answer = 0;
    Send(0xec150cf5, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(kOutKey, Word(&answer));
    });
    return *reinterpret_cast<const bool *>(&answer); // the byte as the renderer left it
}

void cTSAudioSoundManager::Request42(uint32_t a, uint32_t b, uint32_t c, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0x220bf5>()(this, a, b,
                                                                                                                c, flag);
    Post(0x6c2b8ce3, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(0x8c2b9401, b);
        e->SetUint32(0xac2b940c, c);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request43(uint32_t a, uint32_t b, float c, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, float, uint32_t), 0x21ff8d>()(this, a, b, c,
                                                                                                             flag);
    Post(0x8c2b8cfa, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(0x8c2b9401, b);
        e->SetFloat(0xac2b940c, c);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request44(uint32_t a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, float), 0x2206ca>()(this, a, b);
    Post(0xcc488a33, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0xec488a52, b);
    });
}

void cTSAudioSoundManager::Request45(uint32_t a, float b, float c)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, float, float), 0x22029e>()(this, a, b, c);
    Post(0xcc488a38, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0xec488a56, b);
        e->SetFloat(0x8c488fef, c);
    });
}

void cTSAudioSoundManager::Request46(uint32_t a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, float), 0x2201f9>()(this, a, b);
    Post(0xac5a4651, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0x8c5a463d, b);
    });
}

void cTSAudioSoundManager::Request47(uint32_t a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, float), 0x2200ae>()(this, a, b);
    Post(0xac5a4657, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0x8c5a463d, b);
    });
}

void cTSAudioSoundManager::Request48(uint32_t a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, float), 0x2207ea>()(this, a, b);
    Post(0x2c5b9799, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0x8c5a463d, b);
    });
}

void cTSAudioSoundManager::Request49(uint32_t a, float b)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, float), 0x2207ac>()(this, a, b);
    Post(0x2c5b979e, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetFloat(0x8c5a463d, b);
    });
}

void cTSAudioSoundManager::Request50(uint32_t a, uint32_t b, int32_t c)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, int32_t), 0x220741>()(this, a, b, c);
    Post(0xc77381f, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(0xc683d79, b);
        e->SetSint32(0xc683d7d, c);
    });
}

void cTSAudioSoundManager::Request51(uint32_t a, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x22001a>()(this, a, flag);
    Post(0x2cbf1a22, [&](cITSAudioEvent *e) {
        e->SetUint32(0x8bd63087, a);
        e->SetUint32(kFlagKey, uint8_t(flag));
    });
}

void cTSAudioSoundManager::Request52(uint32_t a)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t), 0x21ff2c>()(this, a);
    Post(0x2cbf1a2b, [&](cITSAudioEvent *e) { e->SetUint32(0xacbf1a9f, a); });
}

void cTSAudioSoundManager::Request53(uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return OriginalFunction<void(__thiscall *)(void *, uint32_t), 0x220828>()(this, flag);
    Post(0xd7c4339, [&](cITSAudioEvent *e) { e->SetUint32(kFlagKey, uint8_t(flag)); });
}

// Slot 54: the value goes in as a property under the key (the argument's own address).
void cTSAudioSoundManager::Request54(uint32_t a, uint32_t key, void *value)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, void *), 0x22012b>()(this, a, key, value);
    Post(0xedc86cf8, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(0xedc89abd, key);
        e->SetProperty(&key, value);
    });
}

bool cTSAudioSoundManager::Query55(uint32_t a)
{
    if (!mRenderer) return false;
    if (OnRendererThread()) return OriginalFunction<bool(__thiscall *)(void *, uint32_t), 0x21e9e5>()(this, a);
    uint8_t answer = 0;
    Send(0x4e710074, [&](cITSAudioEvent *e) {
        e->SetUint32(kInstanceKey, a);
        e->SetUint32(kOutKey, Word(&answer));
    });
    return *reinterpret_cast<const bool *>(&answer); // the byte as the renderer left it
}

// 0x21aa95: the voice service's slot 11.
bool cTSAudioSoundManager::VoiceServiceSlot11(uint32_t a)
{
    ServiceRef voice(kVoiceId, kVoiceIid);
    return voice.p ? RawSlot<11, bool>(voice.p, a) : false;
}

// 0x21beb3: modes 0..3 (mono, stereo, 4.0, 5.1); nothing for the current mode. The renderer's slot 41 object is held
// while the voice service switches (slots 4, 12, 3) and the renderer is told (slot 11, event 0x6ebaf256).
bool cTSAudioSoundManager::SetSpeakerMode(uint32_t mode)
{
    if (mode > 3) return false;
    void *const device = RawSlot<41, void *>(mRenderer.Get());
    if (!device) return false;
    ServiceRef voice(kVoiceId, kVoiceIid);
    if (!voice.p) return false;
    uint32_t current;
    if (RawSlot<13, bool>(voice.p, &current) && current == mode) return false;
    RawSlot<1>(device);
    RawSlot<4>(voice.p);
    RawSlot<12>(voice.p, mode);
    RawSlot<3>(voice.p);
    RawSlot<11>(mRenderer.Get(), 0x6ebaf256u, 0u, 0u, 0u, 0u);
    RawSlot<2>(device);
    return true;
}

// 0x21be44: the name (case-insensitive) to the mode, stereo when unknown.
bool cTSAudioSoundManager::SetSpeakerModeByName(const cIGZString *name)
{
    uint32_t mode;
    if (name->IsEqual(GameAddress<const char>(0xda80a4), false))
        mode = 0;
    else if (name->IsEqual(GameAddress<const char>(0xda80ac), false))
        mode = 1;
    else if (name->IsEqual(GameAddress<const char>(0xda80b4), false))
        mode = 2;
    else
        mode = name->IsEqual(GameAddress<const char>(0xda80b8), false) ? 3 : 1;
    return SetSpeakerMode(mode);
}

void cTSAudioSoundManager::Request59(const cIGZString *text, uint32_t a, uint32_t b)
{
    if (!mRenderer) return;
    if (OnRendererThread())
        return OriginalFunction<void(__thiscall *)(void *, const cIGZString *, uint32_t, uint32_t), 0x21fba9>()(this, text,
                                                                                                               a, b);
    Post(0x2d41d87e, [&](cITSAudioEvent *e) {
        e->SetUint32(0xbd63077, a);
        e->SetUint32(0xedd18e72, b);
        e->SetText(0x2d41d97a, text);
    });
}

// 0x21ac60 (607 B): a sound's display name, in a static string: with the testing cheats, the original's namer 0x21efa5,
// else the name maps (by id, then by either word) or the namer 0x21fd55 for the object's own ids; "0x%llx" otherwise.
const char *cTSAudioSoundManager::GetSoundName(uint32_t instance, uint32_t resource)
{
    static AudioResourceString name;
    cRZString &text = name.s;
    cIGZString *const out = reinterpret_cast<cIGZString *>(&text);
    using Namer = bool(__thiscall *)(void *, uint32_t, uint32_t, cIGZString *);
    const auto hex = [&]() {
        text.Sprintf(GameAddress<char>(0xda9ba4), uint64_t(resource) << 32 | instance);
        return text.ToChar();
    };
    if (!CheatsEnabled()) return hex();
    if (OriginalFunction<Namer, 0x21efa5>()(this, instance, resource, out)) return text.ToChar();
    if (mUseNameMaps) {
        const uint64_t id = uint64_t(resource) << 32 | instance;
        auto found = mNames348.find(id);
        if (found != mNames348.end()) return found->second.s.ToChar();
        found = mNames368.find(id);
        if (found != mNames368.end()) return found->second.s.ToChar();
        for (AudioNameMap *names : {&mNames348, &mNames368})
            for (auto &entry : *names) {
                const uint32_t low = uint32_t(entry.first), high = uint32_t(entry.first >> 32);
                if ((low && low == instance) || (high && high == resource)) return entry.second.s.ToChar();
            }
        return hex();
    }
    if (OriginalFunction<Namer, 0x21fd55>()(this, instance, resource, out)) return hex();
    const auto object = mObjects.find(instance);
    if (object == mObjects.end()) return hex();
    bool named;
    {
        AudioRef held(reinterpret_cast<cIGZUnknown *>(uintptr_t(object->second)));
        const uint32_t objectResource = RawSlot<31>(held.Get());
        const uint32_t objectInstance = RawSlot<30>(held.Get());
        named = OriginalFunction<Namer, 0x21fd55>()(this, objectInstance, objectResource, out);
    } // the object is released before the fallback text is formatted
    return named ? text.ToChar() : hex();
}

// 0x21abb8: the log channel of a sound category.
uint32_t cTSAudioSoundManager::GetCategoryLogChannel(uint32_t category)
{
    static constexpr uint32_t kChannels[9] = {0xff5f24, 0xff5f28, 0xff5f14, 0xff5f1c, 0xff5f18,
                                              0xff5f20, 0xff5f2c, 0xff5f30, 0xff5f34};
    return *GameAddress<uint32_t>(category < 9 ? kChannels[category] : 0xff5f10);
}

namespace {
// The original's curve evaluation 0xc0a6a1 (the answer in st0).
double EvaluateCurve(AudioCurve *curve, float x)
{
    return OriginalFunction<double(__thiscall *)(void *, float), 0xc0a6a1>()(curve, x);
}
} // namespace

float cTSAudioSoundManager::EvaluateCurve1B0(float x) { return static_cast<float>(EvaluateCurve(&mCurves[5], x) * mFloat2DC); }
float cTSAudioSoundManager::EvaluateCurve160(float x) { return static_cast<float>(EvaluateCurve(&mCurves[0], x)); }
float cTSAudioSoundManager::EvaluateCurve170(float x) { return static_cast<float>(EvaluateCurve(&mCurves[1], x)); }
float cTSAudioSoundManager::EvaluateCurve180(float x) { return static_cast<float>(EvaluateCurve(&mCurves[2], x)); }
float cTSAudioSoundManager::EvaluateCurve190(float x) { return static_cast<float>(EvaluateCurve(&mCurves[3], x)); }
float cTSAudioSoundManager::EvaluateCurve1A0(float x) { return static_cast<float>(EvaluateCurve(&mCurves[4], x)); }

// 0x21a1d5: the enabled category's volume times its fade and the master fade (two x87 products in double, at the
// calling thread's x87 precision: src/misc/X87.h). The second rounding is the x87's step as written; the float
// conversion after it rounds the same way, so only the first changes the result.
float cTSAudioSoundManager::GetCategoryVolume(uint32_t category)
{
    if (category >= 9 || !mCategoryEnabled[category]) return 0.0f;
    const double faded = x87::Rounded(double(mCategoryVolumes[category]) * mCategoryFades[category].value);
    return static_cast<float>(x87::Rounded(faded * mMasterFade.value));
}

// 0x21a206: the renderer's slot 4 object's slot 9 float.
float cTSAudioSoundManager::GetRendererFloat()
{
    float value;
    RawSlot<9>(RawSlot<4, void *>(mRenderer.Get()), &value);
    return value;
}

// ---- the faces ----

// 0x219430 (1795 B): the renderer's own events (1: a sound to start from a message, 2: one to stop, 0x6ebaf256: the
// device changed), else a cTSAudioEvent run by its id's worker.
bool cTSAudioSoundManager::HandleEvent(cIGZUnknown *event)
{
    switch (RawSlot<4>(event)) {
    case 1: {
        void *const first = RawSlot<6, void *>(event);
        void *const second = RawSlot<6, void *>(event);
        const uint32_t c = RawSlot<14>(first);
        const uint32_t a = RawSlot<9>(second);
        return OriginalFunction<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t),
                                0x220d0a>()(this, a, 0u, c, 0u, 5u, 0u);
    }
    case 2: {
        const uint32_t a = RawSlot<9>(RawSlot<6, void *>(event));
        return OriginalFunction<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0x221c1e>()(this, a, 0u, 1u);
    }
    case 0x6ebaf256:
        OriginalFunction<void(__thiscall *)(void *), 0x21fc9e>()(this);
        return true;
    default:
        break;
    }
    AudioRef held;
    if (!event->QueryInterface(kEventIid, held.AsPPVoid())) return false;
    auto *const ev = static_cast<cITSAudioEvent *>(static_cast<cIGZUnknown *>(held.Get()));
    using Handler = bool(__thiscall *)(void *, cITSAudioEvent *);
    using Flag1 = void(__thiscall *)(void *, uint32_t);
    switch (ev->GetEventId()) {
    case 0xd96318d: return OriginalFunction<Handler, 0x21e502>()(this, ev);
    case 0xc77381f: return OriginalFunction<Handler, 0x21e3c2>()(this, ev);
    case 0xb8d81d5: return OriginalFunction<Handler, 0x21e0ca>()(this, ev);
    case 2:
    case 0x6a451908: return OriginalFunction<Handler, 0x21e80e>()(this, ev);
    case 0xd963193: return OriginalFunction<Handler, 0x21e465>()(this, ev);
    case 0x2a5b69c0: return OriginalFunction<Handler, 0x21e316>()(this, ev);
    case 0x2c5b9799: return OriginalFunction<Handler, 0x21e432>()(this, ev);
    case 0x2c5b979e: return OriginalFunction<Handler, 0x21e3ff>()(this, ev);
    case 0x4a451915: return OriginalFunction<Handler, 0x21e5e2>()(this, ev);
    case 0x4bb54186: return OriginalFunction<Handler, 0x21de95>()(this, ev);
    case 0x4e66585c: return OriginalFunction<Handler, 0x21e087>()(this, ev);
    case 0x6bb5418a: return OriginalFunction<Handler, 0x21e751>()(this, ev);
    case 0x6bb54195: return OriginalFunction<Handler, 0x21e02d>()(this, ev);
    case 0x6c0fd27e: return OriginalFunction<Handler, 0x21e1e2>()(this, ev);
    case 0x6c2b8ce3: return OriginalFunction<Handler, 0x21e623>()(this, ev);
    case 0x8bc77093: return OriginalFunction<Handler, 0x21e27d>()(this, ev);
    case 0x8bdf1f98: return OriginalFunction<Handler, 0x21e04a>()(this, ev);
    case 0x8c2b8cfa: return OriginalFunction<Handler, 0x21e228>()(this, ev);
    case 0x8dfe7c03: return OriginalFunction<Handler, 0x21e870>()(this, ev);
    case 0xac0fd2a2: return OriginalFunction<Handler, 0x21e1ac>()(this, ev);
    case 0xac5a4651: return OriginalFunction<Handler, 0x21e2e3>()(this, ev);
    case 0xac5a4657: return OriginalFunction<Handler, 0x21e2b0>()(this, ev);
    case 0xad9f9fee: return OriginalFunction<Handler, 0x21dfae>()(this, ev);
    case 0xca45190f: return OriginalFunction<Handler, 0x21e59f>()(this, ev);
    case 0xec150cf5: return OriginalFunction<Handler, 0x21e675>()(this, ev);
    case 0xcbb78f7a: return OriginalFunction<Handler, 0x21e8f5>()(this, ev);
    case 0xcc488a33: return OriginalFunction<Handler, 0x21e38f>()(this, ev);
    case 0xcc488a38: return OriginalFunction<Handler, 0x21e34c>()(this, ev);
    case 0xea451903: return OriginalFunction<Handler, 0x21e6b2>()(this, ev);
    case 0xec7153c5: return OriginalFunction<Handler, 0x21def7>()(this, ev);
    case 0xec773209: return OriginalFunction<Handler, 0x21e92b>()(this, ev);
    // Run, then false.
    case 0x2dcae8ef: OriginalFunction<Handler, 0x21e7c1>()(this, ev); return false;
    case 0x6a95cba2: OriginalFunction<Handler, 0x21dff1>()(this, ev); return false;
    case 0x4a8c52c8:
    case 0xabb5419e: UpdateSounds(0); return false;
    case 0xcaa44ca7: {
        const uint32_t a = ev->GetUint32(0x6d03f57a);
        const bool flag = ev->GetUint32(kFlagKey) != 0;
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x221a18>()(this, flag, a);
        return false;
    }
    // Run, then true.
    case 0xd7c4339: OriginalFunction<Flag1, 0x220828>()(this, ev->GetUint32(kFlagKey) != 0); return true;
    case 0x2cbf1a22: {
        const bool flag = ev->GetUint32(kFlagKey) != 0;
        const uint32_t a = ev->GetUint32(0x8bd63087);
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x22001a>()(this, a, flag);
        return true;
    }
    case 0x2cbf1a2b: OriginalFunction<Flag1, 0x21ff2c>()(this, ev->GetUint32(0xacbf1a9f)); return true;
    case 0x2d41d87e:
        if (CheatsEnabled()) {
            AudioResourceString text;
            ev->GetText(0x2d41d97a, reinterpret_cast<cIGZString *>(&text.s));
            const uint32_t b = ev->GetUint32(0xedd18e72);
            const uint32_t a = ev->GetUint32(0xbd63077);
            OriginalFunction<void(__thiscall *)(void *, const cIGZString *, uint32_t, uint32_t), 0x21fba9>()(
                this, reinterpret_cast<cIGZString *>(&text.s), a, b);
        }
        return true;
    case 0x4e710074: {
        const uint32_t a = ev->GetUint32(kInstanceKey);
        auto *const answer = reinterpret_cast<uint8_t *>(uintptr_t(ev->GetUint32(kOutKey)));
        if (answer) *answer = OriginalFunction<bool(__thiscall *)(void *, uint32_t), 0x21e9e5>()(this, a);
        return true;
    }
    case 0x8d238daa: OriginalFunction<Flag1, 0x22011e>()(this, ev->GetUint32(kFlagKey) != 0); return true;
    case 0xccdde023:
        Log(0x6ba, 6, 0xda96d0);
        RawSlot<1>(&mFinalEventSignal.s);
        return true;
    case 0xed1e3aca:
        mInt38 = ev->GetSint32(0xd1e3ae8);
        mInt3C = ev->GetSint32(0xcd1e509f);
        return true;
    case 0xedc86cf8: {
        uint32_t key = ev->GetUint32(0xedc89abd);
        void *const value = ev->GetProperty(&key);
        const uint32_t a = ev->GetUint32(kInstanceKey);
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t, void *), 0x22012b>()(this, a, key, value);
        return true;
    }
    case 0xeedbcdc7: {
        const uint32_t b = ev->GetUint32(0xac5b5b4f);
        const uint32_t a = ev->GetUint32(kInstanceKey);
        OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x2209f1>()(this, a, b);
        return true;
    }
    default: return false;
    }
}

// 0x21d15c: the per-update work (UpdateSpeakerConfig, UpdateSounds), the elapsed total, then the master and category fades.
void cTSAudioSoundManager::Update(uint32_t elapsed)
{
    UpdateSpeakerConfig();
    mElapsed += elapsed;
    UpdateSounds(elapsed);
    mMasterFade.Step(static_cast<float>(static_cast<int32_t>(elapsed)));
    for (AudioRamp &fade : mCategoryFades) fade.Step(static_cast<float>(static_cast<int32_t>(elapsed)));
}

// 0x219385: the finished sound's id to the renderer as event 0x6a451908 (renderer not checked).
void cTSAudioSoundManager::OnSoundComplete(cIGZUnknown *sound)
{
    cITSAudioEvent *const e = NewEvent(0x6a451908);
    const uint32_t id = RawSlot<12>(sound);
    e->SetUint32(kInstanceKey, id);
    e->SetUint32(kFlagKey, 1);
    e->SetUint32(0x2d0d0fac, 1);
    RawSlot<12>(mRenderer.Get(), e);
    e->Release();
}

// 0x219b33 (964 B): always true.
bool cTSAudioSoundManager::DoMessage(void *message)
{
    switch (As<ifaces::cIGZMessage2Standard>(message)->GetType()) {
    case 0x6d21507b: {
        AudioRef standard;
        As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kStandardIid, standard.AsPPVoid());
        if (standard) mByte208 = static_cast<const uint8_t *>(RawSlot<15, void *>(standard.Get()))[4];
        break;
    }
    case 0xb6:
        OriginalFunction<void(__thiscall *)(void *, void *), 0xc0a86a>()(&mCurves[0], &mString1C0);
        Request32(0, 0xffffffffu);
        break;
    case 0xb3: {
        cITSAudioEvent *const e = NewEvent(0x8d238daa);
        e->SetUint32(kFlagKey, 1);
        RawSlot<12>(mRenderer.Get(), e);
        Request53(0);
        Request32(0, 0xffffffffu);
        e->Release();
        break;
    }
    case 0xb2:
        OriginalFunction<void(__thiscall *)(void *, void *), 0xc0a86a>()(&mCurves[0], &mString1E0);
        Request53(1);
        break;
    case 0x4d213401: {
        AudioRef standard;
        As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kStandardIid, standard.AsPPVoid());
        if (standard)
            OriginalFunction<void(__thiscall *)(void *, uint32_t), 0x21d654>()(this, As<ifaces::cIGZMessage2Standard>(standard.Get())->GetData1());
        break;
    }
    case 0xcbccdece: {
        AudioRef standard;
        As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kStandardIid, standard.AsPPVoid());
        gz::AnyUint32 value(As<ifaces::cIGZMessage2Standard>(standard.Get())->GetData1());
        Request54(0xedc89ac8, 0xcd201058, &value);
        break;
    }
    case 0xcdafda21: {
        void *const state = GetCachedSlot23();
        if (!state) break;
        const uint32_t mode = RawSlot<5>(state);
        if (mode != 0xcc18133f && mode != 0xcc181346 && mode != 0xcc18134a && mode != 0xbec20c2 && mode != 0xc18069d) break;
        Request53(1);
        cITSAudioEvent *const e = NewEvent(0x8d238daa);
        e->SetUint32(kFlagKey, 0);
        RawSlot<12>(mRenderer.Get(), e);
        Request32(1, uint32_t(-0x11));
        Request31(0, uint32_t(-0x11));
        e->Release();
        break;
    }
    case 0xece20be8: {
        AudioRef standard;
        As<ifaces::cIGZMessage2Standard>(message)->QueryInterface(kStandardIid, standard.AsPPVoid());
        if (!standard) break;
        cITSAudioEvent *const e = NewEvent(0xed1e3aca);
        e->SetSint32(0xd1e3ae8, As<ifaces::cIGZMessage2Standard>(standard.Get())->GetData1());
        e->SetSint32(0xcd1e509f, RawSlot<19, int32_t>(standard.Get()));
        RawSlot<12>(mRenderer.Get(), e);
        e->Release();
        break;
    }
    default: break;
    }
    return true;
}

// 0x21aedd.
bool cTSAudioSoundManager::OnTick(uint32_t)
{
    FlushPendingEvents();
    return true;
}

// 0x21d5e9: the pending events to the renderer's slot 8 in order while its slot 41 object is held.
void cTSAudioSoundManager::FlushPendingEvents()
{
    if (mPending.empty()) return;
    void *const device = RawSlot<41, void *>(mRenderer.Get());
    if (!device) return;
    RawSlot<1>(device);
    while (!mPending.empty()) {
        RawSlot<8>(mRenderer.Get(), mPending.front().Get());
        mPending.pop_front();
    }
    RawSlot<2>(device);
}

// ---------------------------------------------------------------------------------------------
// The startup workers (lane IY part 2)

namespace {

// A cRZString local (built and destroyed by hand, as the original's inlined code does).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const cIGZString &text) { s.Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
};

// An AnyString local (the game's vtable: AnyString is not in the standalone shape); its string released at scope end.
struct LocalAnyString {
    AnyString a{GameAddress<const void>(gzany::kStringVTable), nullptr};
    LocalAnyString() = default;
    LocalAnyString(const LocalAnyString &) = delete;
    LocalAnyString &operator=(const LocalAnyString &) = delete;
    ~LocalAnyString() { a.Destruct(); }
};

// The string-keyed set's slot 9 (GetPropertyValue) for a name (a C string of the game's), the key string gone after.
bool GetNamedProperty(void *set, const char *name, void *out)
{
    GZServiceString key(name);
    return As<ifaces::cIGZPropertySetString>(set)->GetPropertyValue(&key, out);
}

} // namespace

// 0x217fbb (cdecl): the float property `name` of the set, `fallback` without a set or when the set lacks it.
float __cdecl SoundDefaults::GetFloat(void *set, const GZServiceString *name, float fallback)
{
    if (!set) return fallback;
    gz::AnyFloat32 value(0.0f);
    return As<ifaces::cIGZPropertySetString>(set)->GetPropertyValue(const_cast<GZServiceString *>(name), static_cast<cIGZAny *>(&value)) ? value.mValue : fallback;
}

// 0x218012 (cdecl): the same for a uint32 property.
uint32_t __cdecl SoundDefaults::GetUint(void *set, const GZServiceString *name, uint32_t fallback)
{
    if (!set) return fallback;
    gz::AnyUint32 value(0);
    return As<ifaces::cIGZPropertySetString>(set)->GetPropertyValue(const_cast<GZServiceString *>(name), static_cast<cIGZAny *>(&value)) ? value.mValue : fallback;
}

// 0x23b687 (cdecl): a token as an unsigned number (strtoul, base 0); always true.
bool __cdecl SoundDefaults::ParseUint(const cIGZString *token, uint32_t *out)
{
    char *end = nullptr;
    const char *const text = token->ToChar();
    *out = std::strtoul(text, &end, 0);
    return true;
}

// 0x23b6af (cdecl): a token as a float (the game's number reader 0x1b1ab5, rounded to float); always true.
bool __cdecl SoundDefaults::ParseFloat(const cIGZString *token, float *out)
{
    const char *end = nullptr;
    const char *const text = token->ToChar();
    *out = static_cast<float>(rztext::ReadDouble(text, &end));
    return true;
}

// 0x2180f2 (cdecl): the comma-separated numbers of text appended to out; false only if a token does not parse (never:
// ParseUint is always true).
bool __cdecl SoundDefaults::ParseUintList(std::vector<uint32_t> *out, const cIGZString *text)
{
    LocalString rest(*text);
    LocalString token;
    uint32_t value;
    while (reinterpret_cast<cRZStringTokenizer *>(&rest.s)->NextToken(',', &token.s)) {
        if (!ParseUint(token.s.AsInterface(), &value)) return false;
        out->push_back(value);
    }
    return true;
}

// 0x21817d (cdecl): the same for floats.
bool __cdecl SoundDefaults::ParseFloatList(std::vector<float> *out, const cIGZString *text)
{
    LocalString rest(*text);
    LocalString token;
    float value;
    while (reinterpret_cast<cRZStringTokenizer *>(&rest.s)->NextToken(',', &token.s)) {
        if (!ParseFloat(token.s.AsInterface(), &value)) return false;
        out->push_back(value);
    }
    return true;
}

// 0x218208 (cdecl): each float truncated (to 64 bits, __ftol3, the low word kept) into out; returns the end.
uint32_t *__cdecl SoundDefaults::CopyTruncated(const float *first, const float *last, uint32_t *out)
{
    for (; first != last; ++first) *out++ = static_cast<uint32_t>(static_cast<int64_t>(*first));
    return out;
}

// 0x21f164 (2629 B). The original's inlined names, curve strings and list copies in its order.
bool cTSAudioSoundManager::LoadSoundDefaults()
{
    uint32_t key[4] = {0xebfee33f, 0x4c1940c5, *GameAddress<uint32_t>(0x10cc4c8), *GameAddress<uint32_t>(0x10cc4cc)};
    void *const resources = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    if (!As<ifaces::cITSPersistSystem>(resources)->GetResource(reinterpret_cast<const Key16 *>(key), 0x4bb0d269u, mRefB0.AsPPVoid(), 0u, 0u)) {
        Log(0x1096, 2, 0xda9af8); // "Failed to load sound manager tuning values"
        return false;
    }
    // The names are the game's (pointers at 0xff4518..0xff4580).
    auto name = [](uint32_t cell) { return *GameAddress<const char *>(cell); };
    auto getFloat = [&](uint32_t cell, float fallback) {
        GZServiceString text(name(cell));
        return SoundDefaults::GetFloat(mRefB0.Get(), &text, fallback);
    };
    auto getUint = [&](uint32_t cell) {
        GZServiceString text(name(cell));
        return SoundDefaults::GetUint(mRefB0.Get(), &text, 0);
    };
    const float scale = *GameAddress<float>(0xda9cc0); // 65535
    mFloat2B8 = getFloat(0xff4518, 0.6f);              // SecondaryListenerInfluence
    mFloat2C4 = getFloat(0xff454c, 1.0f);              // RolloffAspectRatio
    mFloat2D0 = getFloat(0xff4550, 2000.0f) / scale;   // RoomObstructionFilterLevel
    mFloat2D4 = getFloat(0xff4554, 400.0f) / scale;    // LevelObstructionFilterLevel
    mFloat2D8 = getFloat(0xff4560, 1.0f);              // LevelObstructionVolumeLevel
    mFloat2DC = getFloat(0xff4558, 1.0f);              // ReverbWetLevel
    for (uint32_t i = 0; i < 4; ++i) mForceSampleRates[i] = getUint(0xff4564 + 4 * i); // Force{Sfx,Vox,Music,Ambience}SampleRate
    mAdjustSampleRates[0] = getFloat(0xff4574, 0.0f);  // AdjustSfxSampleRate
    mAdjustSampleRates[1] = getFloat(0xff4578, 0.0f);  // AdjustVoxSampleRate
    mFloat2FC = getFloat(0xff4580, 1.0f);              // ListenerAttenuationPositionsScalar

    LocalAnyString value;
    LocalString curve;
    auto lookup = [&](uint32_t cell) { return GetNamedProperty(mRefB0.Get(), name(cell), &value.a); };
    // The filter curves: the two listener curve strings (through their vtables), then the six curves.
    const char *const curveDefault = GameAddress<char>(0xda9b3c); // "0,400,0.9,22050,1,65535"
    VirtualSlot<cRZString *(__cdecl *)(void *, const char *)>(&mString1C0, 31)(&mString1C0, curveDefault);
    VirtualSlot<cRZString *(__cdecl *)(void *, const char *)>(&mString1E0, 31)(&mString1E0, curveDefault);
    if (lookup(0xff451c)) RawSlot<16>(&mString1C0, value.a.mString); // NeighborhoodListenerLowPassFilterCurve
    if (lookup(0xff4520)) RawSlot<16>(&mString1E0, value.a.mString); // PrimaryListenerLowPassFilterCurve
    OriginalFunction<void(__thiscall *)(void *, void *), 0xc0a86a>()(&mCurves[0], &mString1C0);
    // Secondary..., Direction..., GlobalLowPass..., GlobalHighPass...FilterCurve, RoomSizeReverbWetLevel ("0,1,1,0.25").
    const uint32_t curveNames[5] = {0xff4524, 0xff4528, 0xff452c, 0xff4530, 0xff455c};
    for (uint32_t i = 0; i < 5; ++i) {
        curve.s.Sprintf(i == 4 ? GameAddress<char>(0xda9b54) : curveDefault);
        if (lookup(curveNames[i])) RawSlot<16>(&curve.s, value.a.mString);
        OriginalFunction<void(__thiscall *)(void *, void *), 0xc0a86a>()(&mCurves[i + 1], &curve.s);
    }

    mTimerPeriods[0] = 50;
    mTimerPeriods[1] = 35;
    mTimerPeriods[2] = 20;
    if (lookup(0xff4538)) { // CallbackTimerPeriods
        std::vector<float> list;
        SoundDefaults::ParseFloatList(&list, value.a.mString);
        SoundDefaults::CopyTruncated(list.data(), list.data() + (std::min)(list.size(), size_t(3)), mTimerPeriods);
    }
    const float pitch[5] = {0.0f, 0.0f, 1.0f, 1.2f, 2.0f};
    std::copy(pitch, pitch + 5, mPitchBySpeed);
    if (lookup(0xff4544)) { // SimSpeedPitchMultipliers
        std::vector<float> list;
        SoundDefaults::ParseFloatList(&list, value.a.mString);
        std::copy(list.begin(), list.begin() + (std::min)(list.size(), size_t(5)), mPitchBySpeed);
    }
    const float volume[5] = {0.0f, 0.0f, 1.0f, 0.9f, 0.66f};
    std::copy(volume, volume + 5, mVolumeBySpeed);
    if (lookup(0xff4548)) { // SimSpeedVolumeMultipliers
        std::vector<float> list;
        SoundDefaults::ParseFloatList(&list, value.a.mString);
        std::copy(list.begin(), list.begin() + (std::min)(list.size(), size_t(5)), mVolumeBySpeed);
    }
    mMaxSounds[0] = 8;
    mMaxSounds[1] = 16;
    mMaxSounds[2] = 24;
    if (lookup(0xff453c)) { // MaxPlayingSoundsPerPerformanceLevel
        std::vector<uint32_t> list;
        SoundDefaults::ParseUintList(&list, value.a.mString);
        std::copy(list.begin(), list.begin() + (std::min)(list.size(), size_t(3)), mMaxSounds);
    }
    std::fill(&mMaxSoundsByGroup[0][0], &mMaxSoundsByGroup[0][0] + 27, *GameAddress<uint32_t>(0xda8e6c)); // -1
    if (lookup(0xff4540)) { // ...ByGroup: group by group, three levels each; 0 means -1 (no limit)
        std::vector<uint32_t> list;
        SoundDefaults::ParseUintList(&list, value.a.mString);
        size_t next = 0;
        for (uint32_t group = 0; group < 9 && next < list.size(); ++group)
            for (uint32_t level = 0; level < 3 && next < list.size(); ++level) {
                const uint32_t n = list[next++];
                mMaxSoundsByGroup[level][group] = n ? n : 0xffffffffu;
            }
    }
    Log(0x1133, 5, 0xda9b60); // "Read sound manager tuning values.\n"
    return true;
}

// 0x220320.
void cTSAudioSoundManager::ApplySettings()
{
    ServiceRef settings(0x0ac1887e, 0x4bb0d269); // 0x1cbaf3
    gz::AnyUint32 vox(100), fx(100), music(100), ambience(100);
    gz::AnyBoolean voxOn(1), fxOn(1), musicOn(1), ambienceOn(1);
    gz::AnyUint32 performance(0);
    AnyString mode;
    mode.Construct(GameAddress<char>(0xda80ac)); // "stereo"
    if (settings.p) {
        GetNamedProperty(settings.p, GameAddress<char>(0xda94cc), &fx);          // FXVolume
        GetNamedProperty(settings.p, GameAddress<char>(0xda94d8), &ambience);    // AmbienceVolume
        GetNamedProperty(settings.p, GameAddress<char>(0xda94e8), &vox);         // VOXVolume
        GetNamedProperty(settings.p, GameAddress<char>(0xda7e40), &music);       // MusicVolume
        GetNamedProperty(settings.p, GameAddress<char>(0xda94b8), &performance); // AudioPerformance
        GetNamedProperty(settings.p, GameAddress<char>(0xda94f4), &voxOn);       // SoundMasterGroupVoxEnabled
        GetNamedProperty(settings.p, GameAddress<char>(0xda7e4c), &musicOn);     // SoundMasterGroupMusicEnabled
        GetNamedProperty(settings.p, GameAddress<char>(0xda952c), &ambienceOn);  // SoundMasterGroupAmbienceEnabled
        GetNamedProperty(settings.p, GameAddress<char>(0xda9510), &fxOn);        // SoundMasterGroupFXEnabled
        GetNamedProperty(settings.p, GameAddress<char>(0xda954c), &mode);        // SoundOutputMode
    }
    // The percentages through double, as the original converts them (0x220571: cvtdq2pd with the unsigned fix-up).
    const float percent = *GameAddress<float>(0xd9fac4); // 100
    auto level = [&](const gz::AnyUint32 &v) { return static_cast<float>(static_cast<double>(v.mValue)) / percent; };
    Request19(0, level(fx));
    Request19(1, level(vox));
    Request19(4, level(music));
    Request19(2, level(ambience));
    Request51(0, fxOn.mValue);
    Request51(1, voxOn.mValue);
    Request51(4, musicOn.mValue);
    Request51(2, ambienceOn.mValue);
    Request52(performance.mValue);
    SetSpeakerModeByName(reinterpret_cast<const cIGZString *>(mode.mString));
    mode.Destruct();
}

// 0x21e9ab.
bool cTSAudioSoundManager::HasConfigProperty(uint32_t key, uint32_t id)
{
    const auto it = mRefs318.find(key);
    if (it == mRefs318.end()) return false;
    return RawSlot<10, bool>(it->second.Get(), &id);
}

// 0x21dc23.
bool cTSAudioSoundManager::GetConfigProperty(uint32_t key, uint32_t id, cIGZAny *out)
{
    const auto it = mRefs318.find(key);
    if (it == mRefs318.end()) return false;
    return RawSlot<9, bool>(it->second.Get(), &id, out);
}

// 0x2221e8: 0xcd201058 set (non-zero) turns the speaker mode off (0); otherwise the mode 0xac19638c (2 and volume 1 when
// absent; modes 2..4 take their pitch and volume multipliers, others 1 and 1), then 0xdc99afb into +0x344.
void cTSAudioSoundManager::UpdateSpeakerConfig()
{
    constexpr uint32_t kConfig = 0xedc89ac8;
    gz::AnyUint32 value(0);
    if (HasConfigProperty(kConfig, 0xcd201058)) {
        GetConfigProperty(kConfig, 0xcd201058, &value);
        if (value.mValue) {
            mWord338 = 0;
            return;
        }
    }
    if (!HasConfigProperty(kConfig, 0xac19638c)) {
        mWord338 = 2;
        mFloat33C = 1.0f;
    } else {
        GetConfigProperty(kConfig, 0xac19638c, &value);
        const uint32_t mode = value.mValue;
        mWord338 = mode;
        if (mode - 2 < 3) {
            mFloat33C = mPitchBySpeed[mode];
            mFloat340 = mVolumeBySpeed[mode];
        } else {
            mFloat33C = 1.0f;
            mFloat340 = 1.0f;
        }
    }
    if (HasConfigProperty(kConfig, 0xdc99afb)) {
        GetConfigProperty(kConfig, 0xdc99afb, &value);
        mWord344 = value.mValue;
    }
}

// 0x222147: every sound in +0x60 gets the elapsed time (slot 83); one not finished (slot 71 zero), started (62) and not
// paused (65 with -1) counts as playing: in +0x70 when its slot 57 (9) is set, otherwise in +0x6c and by its category
// (slot 36) in +0x74. +0x28 is held raised meanwhile.
void cTSAudioSoundManager::UpdateSounds(uint32_t elapsed)
{
    ++mWord28;
    mWord68 = 0;
    mWord6C = 0;
    mWord70 = 0;
    for (uint32_t &n : mWords74) n = 0;
    for (const AudioRef &ref : mList60) {
        cIGZUnknown *const sound = ref.Get();
        RawSlot<83>(sound, elapsed);
        if (RawSlot<71>(sound) == 0 && RawSlot<62, bool>(sound) && !RawSlot<65, bool>(sound, 0xffffffffu)) {
            ++mWord68;
            if (RawSlot<57, bool>(sound, 9u)) {
                ++mWord70;
            } else {
                ++mWord6C;
                ++mWords74[RawSlot<36>(sound)];
            }
        }
    }
    --mWord28;
}
