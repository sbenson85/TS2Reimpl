// cGZSndMgr (lane KJ): the whole class in the standalone shape (src/gz/cGZSndMgr.h).
#include "gz/cGZSndMgr.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StringKeyParser.h"

#include <new>
#include <tuple>
#include <utility>

namespace {

// The sound renderer probe (cached at 0x10f51b4): an original callee nobody has replaced.
cIGZUnknown *SoundProbe() { return OriginalFunction<cIGZUnknown *(__cdecl *)(), 0x9c5b7e>()(); }

// The framework's slot 5 into a reference released at scope end (0x20db69: the persist resource manager).
struct ServiceRef {
    void *p = nullptr;
    ServiceRef(uint32_t id, uint32_t iid)
    {
        if (void *const service = GetServiceObject()) As<ifaces::cIGZFrameWork>(service)->GetSystemService(id, iid, &p);
    }
    ServiceRef(const ServiceRef &) = delete;
    ServiceRef &operator=(const ServiceRef &) = delete;
    ~ServiceRef()
    {
        if (p) static_cast<cIGZUnknown *>(p)->Release();
    }
};
constexpr uint32_t kResourceManagerId = 0x056b906e, kResourceManagerIid = 0x656b8efc;

constexpr uint32_t kCreatorIid = 0x3af9866f;  // slot 15's creators
constexpr uint32_t kCreator2Iid = 0x9b75709e; // slot 25's creators
constexpr uint32_t kSoundIid = 0x206077d2;    // what Play asks for
constexpr uint32_t kUpdatePeriod = 0xfa;      // the renderer's update period for the target (ms)

// Holds the manager's lock (its slots 1 / 2) for the scope.
struct Locked {
    cRZCriticalSection *lock;
    explicit Locked(cRZCriticalSection *l) : lock(l) { lock->Lock(); }
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;
    ~Locked() { lock->Unlock(); }
};

// Raises a busy count for the scope.
struct Busy {
    uint32_t &count;
    explicit Busy(uint32_t &c) : count(c) { ++count; }
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;
    ~Busy() { --count; }
};

// The renderer's slot 30 (add an update target with its period) and the sound's slots 6 (start) and 7 (still busy).
void AddUpdateTarget(cIGZUnknown *renderer, cIGZSndUpdateTarget *target, uint32_t period)
{
    RawSlot<30, void>(renderer, target, period);
}
bool IsRendererReady(cIGZUnknown *renderer) { return RawSlot<38>(renderer) != 0; }
bool Start(cIGZUnknown *sound, uint32_t flags) { return RawSlot<6, bool>(sound, flags, 0u, 1u); }
bool IsStillPlaying(cIGZUnknown *sound) { return RawSlot<7, bool>(sound); }

// A creator's slot 3: asked for an id nobody registered (the six arguments of Create / Create2).
bool AskCreator(cIGZUnknown *creator, uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out)
{
    return RawSlot<3, bool>(creator, id, a, b, iid, out);
}

} // namespace

// ---- construction ----

// 0xb256f5: the members in order, the lock last.
cGZSndMgr::cGZSndMgr() : rz::cRZSystemService(kServiceId, kPriority)
{
    auto *const lock = static_cast<cRZCriticalSection *>(::operator new(sizeof(cRZCriticalSection)));
    mLock = lock ? lock->Construct() : nullptr;
}

// 0xb2582c: Shutdown, the lock freed (its deleting destructor), then the members in reverse order.
cGZSndMgr::~cGZSndMgr() noexcept
{
    cGZSndMgr::Shutdown();
    if (mLock) mLock->DeletingDestructor(1);
    mLock = nullptr;
}

// 0xb22333.
cGZSndMgr *__cdecl cGZSndMgr::Create() { return new cGZSndMgr; }

cGZSndMgr *__fastcall cGZSndMgrAdapters::Construct(void *memory, void *) { return ::new (memory) cGZSndMgr; }

// ---- cIGZUnknown ----

// 0xb25c7f: cIGZSndMgr -> +0x1c (false for a null out), counted through the primary face's AddRef; else the service's.
bool cGZSndMgr::QueryInterface(uint32_t riid, void **out)
{
    if (riid != cIGZSndMgr::kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    if (!out) return false;
    *out = static_cast<cIGZSndMgr *>(this);
    static_cast<rz::cRZSystemService *>(this)->AddRef();
    return true;
}

// 0xb25f0c: the last reference shuts the service down first.
uint32_t cGZSndMgr::Release()
{
    if (rz::cRZUnknownMT::RefCount() == 1) Shutdown();
    return rz::cRZSystemService::Release();
}

// ---- cIGZSystemService ----

// 0xb25c00: once: the renderer held (false without one), the update target registered with it, the resource manager held.
bool cGZSndMgr::Init()
{
    Locked hold(mLock);
    if (mInitialized) return true;
    mInitialized = 1;
    mRenderer.Assign(SoundProbe());
    if (!mRenderer) return false;
    AddUpdateTarget(mRenderer.Get(), static_cast<cIGZSndUpdateTarget *>(this), kUpdatePeriod);
    ServiceRef resources(kResourceManagerId, kResourceManagerIid);
    mResources.Assign(static_cast<cIGZUnknown *>(resources.p));
    return true;
}

// 0xb260d0: the playing list, the aliases, the objects, the creators and the second creators emptied (the update target
// stays registered, the renderer, the resource manager and the second objects held). Always true.
bool cGZSndMgr::Shutdown()
{
    Locked hold(mLock);
    {
        Busy busy(mPlayingBusy);
        mPlaying.clear();
    }
    mAliases.clear();
    {
        Busy busy(mObjectsBusy);
        mObjects.clear();
    }
    {
        Busy busy(mCreatorsBusy);
        mCreators.clear();
    }
    {
        Busy busy(mCreators2Busy);
        mCreators2.clear();
    }
    if (!mMapAC.empty()) {
        for (auto it = mMapAC.begin(); it != mMapAC.end(); ++it) {
        }
        mMapAC.clear();
    }
    mInitialized = 0;
    return true;
}

// ---- cIGZSndMgr ----

// 0xb25980: the resource manager's slot 3 (the arguments, two zero words); false without one.
bool cGZSndMgr::OpenThroughResources(uint32_t a, uint32_t b, uint32_t c)
{
    if (!mResources) return false;
    return RawSlot<3, bool>(mResources.Get(), a, b, c, 0u, 0u);
}

// 0xb262e2.
uint32_t cGZSndMgr::GetAlias(uint32_t name)
{
    Locked hold(mLock);
    const auto it = mAliases.find(name);
    return it == mAliases.end() ? 0 : static_cast<uint32_t>(it->second);
}

// 0xb2606d: the old entry erased first (the new one goes where a new key would).
void cGZSndMgr::SetAlias(uint32_t name, uint64_t id)
{
    Locked hold(mLock);
    const auto it = mAliases.find(name);
    if (it != mAliases.end()) mAliases.erase(it);
    mAliases[name] = id;
}

bool cGZSndMgr::SetWordB4(uint32_t value) // 0xb2604f
{
    mWordB4 = value;
    return true;
}
bool cGZSndMgr::GetWordB4(uint32_t *out) // 0xb2595e
{
    *out = mWordB4;
    return true;
}
bool cGZSndMgr::SetWordB8(uint32_t value) // 0xb2605e
{
    mWordB8 = value;
    return true;
}
bool cGZSndMgr::GetWordB8(uint32_t *out) // 0xb2596f
{
    *out = mWordB8;
    return true;
}

// 0xb25cce.
void cGZSndMgr::RegisterObject(cIGZUnknown *object, uint64_t id)
{
    Locked hold(mLock);
    if (mObjects.find(id) != mObjects.end()) return;
    const ObjectMap::value_type entry(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(object));
    mObjects.insert(entry);
}

// 0xb26322.
void cGZSndMgr::UnregisterObject(uint64_t id)
{
    Locked hold(mLock);
    const auto it = mObjects.find(id);
    if (it != mObjects.end()) mObjects.erase(it);
}

// 0xb2637c.
void cGZSndMgr::UnregisterObjectByPointer(cIGZUnknown *object)
{
    Locked hold(mLock);
    Busy busy(mObjectsBusy);
    for (auto it = mObjects.begin(); it != mObjects.end(); ++it) {
        if (it->second.Get() == object) {
            mObjects.erase(it);
            break;
        }
    }
}

// 0xb25d66: a creator (interface 0x3af9866f) moves to the front; false for null or another kind of object.
bool cGZSndMgr::AddCreator(cIGZUnknown *creator)
{
    if (!creator) return false;
    bool added = false;
    AudioRef kind;
    if (creator->QueryInterface(kCreatorIid, kind.AsPPVoid())) {
        RemoveCreator(creator);
        Locked hold(mLock);
        Busy busy(mCreatorsBusy);
        {
            const AudioRef held(creator);
            mCreators.push_front(held);
        }
        added = true;
    }
    return added;
}

// 0xb25f43.
bool cGZSndMgr::RemoveCreator(cIGZUnknown *creator)
{
    bool removed = false;
    Locked hold(mLock);
    Busy busy(mCreatorsBusy);
    for (auto it = mCreators.begin(); it != mCreators.end(); ++it) {
        if (it->Get() == creator) {
            mCreators.erase(it);
            removed = true;
            break;
        }
    }
    return removed;
}

// 0xb25ad1: the name's id (CRC24 | 0xff000000, CRC32 lowercased).
bool cGZSndMgr::CreateObjectByName(cIGZString *name, uint32_t a, uint32_t b, uint32_t iid, void **out)
{
    const uint32_t low = StringKeyParser::Crc24(name->ToChar(), 0) | 0xff000000u;
    const uint32_t high = StringKeyParser::Crc32(name->ToChar(), 1);
    return CreateObject(uint64_t(high) << 32 | low, a, b, iid, out);
}

// 0xb25b22: false while the renderer is not ready; the registered object's interface, else the first creator's answer.
bool cGZSndMgr::CreateObject(uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out)
{
    if (!IsRendererReady(mRenderer.Get())) return false;
    bool created = false;
    {
        Locked hold(mLock);
        const auto it = mObjects.find(id);
        if (it != mObjects.end()) {
            if (it->second) created = it->second->QueryInterface(iid, out);
            return created;
        }
    }
    Locked hold(mLock);
    Busy busy(mCreatorsBusy);
    for (auto it = mCreators.begin(); it != mCreators.end(); ++it) {
        created = AskCreator(it->Get(), id, a, b, iid, out);
        if (created) break;
    }
    return created;
}

// 0xb2617f.
bool cGZSndMgr::Play(uint64_t id) { return PlayWithFlags(id, mDefaultFlags); }

// 0xb26192: the registered object, else a new sound from Create; queued on the playing list, then started.
bool cGZSndMgr::PlayWithFlags(uint64_t id, uint32_t flags)
{
    cIGZUnknown *sound = nullptr;
    {
        Locked hold(mLock);
        ObjectMap::iterator it;
        {
            const uint32_t busy = mObjectsBusy;
            mObjectsBusy = busy + 1;
            it = mObjects.find(id);
            mObjectsBusy = busy;
        }
        if (it != mObjects.end()) {
            sound = it->second.Get();
            sound->AddRef();
        } else if (!CreateObject(id, 0, 0, kSoundIid, reinterpret_cast<void **>(&sound))) {
            return false;
        }
        AddPlaying(sound);
    }
    sound->Release();
    return Start(sound, flags);
}

// 0xb26257.
bool cGZSndMgr::PlayAlias(uint32_t name, uint32_t unused) { return PlayAliasWithFlags(name, unused, mDefaultFlags); }

// 0xb2626a: the alias's id (0 when none).
bool cGZSndMgr::PlayAliasWithFlags(uint32_t name, uint32_t, uint32_t flags)
{
    uint64_t id = 0;
    {
        Locked hold(mLock);
        const auto it = mAliases.find(name);
        if (it != mAliases.end()) id = it->second;
    }
    return PlayWithFlags(id, flags);
}

// 0xb25928 (no lock).
bool cGZSndMgr::AddPlaying(cIGZUnknown *sound)
{
    const AudioRef held(sound);
    mPlaying.push_back(held);
    return true;
}

// 0xb259a3: the registered object's interface, else the first second creator's answer (in key order).
bool cGZSndMgr::CreateObject2(uint64_t id, uint32_t a, uint32_t b, uint32_t iid, void **out)
{
    bool created = false;
    {
        Locked hold(mLock);
        const auto it = mObjects2.find(id);
        if (it != mObjects2.end()) {
            if (it->second) created = it->second->QueryInterface(iid, out);
            return created;
        }
    }
    Locked hold(mLock);
    Busy busy(mCreators2Busy);
    for (auto it = mCreators2.begin(); it != mCreators2.end(); ++it) {
        created = AskCreator(it->second.Get(), id, a, b, iid, out);
        if (created) break;
    }
    return created;
}

// 0xb25e77: a second creator (interface 0x9b75709e) replaces the key's; false for null or another kind of object.
bool cGZSndMgr::AddCreator2(cIGZUnknown *creator, uint32_t key)
{
    if (!creator) return false;
    void *kind = nullptr;
    if (!creator->QueryInterface(kCreator2Iid, &kind)) return false;
    static_cast<cIGZUnknown *>(kind)->Release();
    RemoveCreator2(key);
    Locked hold(mLock);
    Busy busy(mCreators2Busy);
    const AudioRef held(creator);
    mCreators2[key] = held;
    return true;
}

// 0xb25f95.
bool cGZSndMgr::RemoveCreator2(uint32_t key)
{
    Locked hold(mLock);
    Busy busy(mCreators2Busy);
    const auto it = mCreators2.find(key);
    const bool found = it != mCreators2.end();
    if (found) mCreators2.erase(it);
    return found;
}

// 0xb25fe1.
bool cGZSndMgr::RemoveCreator2ByPointer(cIGZUnknown *creator)
{
    Locked hold(mLock);
    Busy busy(mCreators2Busy);
    for (auto it = mCreators2.begin(); it != mCreators2.end(); ++it) {
        if (it->second.Get() == creator) {
            mCreators2.erase(it);
            return true;
        }
    }
    return false;
}

// 0xb25a73: the key's creator's interface (the creator unchecked).
bool cGZSndMgr::QueryCreator2(uint32_t key, uint32_t iid, void **out)
{
    Locked hold(mLock);
    std::map<uint32_t, AudioRef>::iterator it;
    {
        const uint32_t busy = mCreators2Busy;
        mCreators2Busy = busy + 1;
        it = mCreators2.find(key);
        mCreators2Busy = busy;
    }
    if (it == mCreators2.end()) return false;
    return it->second->QueryInterface(iid, out);
}

// 0xb25dee.
void cGZSndMgr::RegisterObject2(cIGZUnknown *object, uint64_t id)
{
    Locked hold(mLock);
    if (mObjects2.find(id) != mObjects2.end()) return;
    const std::map<uint64_t, AudioRef>::value_type entry(std::piecewise_construct, std::forward_as_tuple(id),
                                                         std::forward_as_tuple(object));
    mObjects2.insert(entry);
}

// 0xb263bf.
void cGZSndMgr::UnregisterObject2(uint64_t id)
{
    Locked hold(mLock);
    const auto it = mObjects2.find(id);
    if (it != mObjects2.end()) mObjects2.erase(it);
}

// 0xb26410.
void cGZSndMgr::UnregisterObject2ByPointer(cIGZUnknown *object)
{
    Locked hold(mLock);
    Busy busy(mObjects2Busy);
    for (auto it = mObjects2.begin(); it != mObjects2.end(); ++it) {
        if (it->second.Get() == object) {
            mObjects2.erase(it);
            break;
        }
    }
}

// ---- cIGZSndUpdateTarget ----

// 0xb26474: the first playing sound dropped once it says it is done.
void cGZSndMgr::Update(uint32_t)
{
    Locked hold(mLock);
    if (mPlaying.empty()) return;
    if (!IsStillPlaying(mPlaying.front().Get())) mPlaying.pop_front();
}
