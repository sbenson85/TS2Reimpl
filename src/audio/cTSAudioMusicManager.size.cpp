// cTSAudioMusicManager (lane KI): the class in the standalone shape (src/audio/cTSAudioMusicManager.h).
#include "audio/cTSAudioMusicManager.h"

#include "audio/cTSAudioEvent.h"
#include "audio/cTSAudioSoundManager.h" // cITSAudioSoundManager
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZSystemService.h"
#include "misc/UtilitiesC.h"     // GetCachedServiceObjectC (the sound manager), KeyMapSlot5Walker
#include "rz/MiscHelpersA.h"     // cRZStd
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StringKeyParser.h"  // cRZStringHelpers::LowerCase
#include "rz/cRZCmdLine.h"       // cIGZCmdLine
#include "rz/nGZLog.h"           // StoreCurrentThreadId

#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The renderer (0x20db8e) and the service 0x212e8c: framework services queried into a reference.
constexpr uint32_t kRendererId = 0x23cecba0, kRendererIid = 0x106077d2;
constexpr uint32_t kService38Id = 0x1af85bfb, kService38Iid = 0x3af85bb7;
void GetService(AudioRef &out, uint32_t id, uint32_t iid)
{
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, out.AsPPVoid());
}

// The framework service cached at 0x10f5194 (0x211051: filled from GetServiceObject the first time).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

cITSAudioSoundManager *SoundManager() { return static_cast<cITSAudioSoundManager *>(GetCachedServiceObjectC()); }
void *SoundProbe() { return OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()(); }

// A log line (the 0x0bb6649b event): a cRZStd for (mode 8, this file's name, line), then its slot 2 (cdecl) with the
// music channel (the word at 0xff5f18), level 5, the format and the arguments.
constexpr uint32_t kSourceFile = 0xda8a78, kMusicChannel = 0xff5f18;
constexpr uint32_t kRestartFormat = 0xda8af8; // "Restarting playing instance %08x for %s.\n"

// The event ids (one per proxy and handler case) and the keys of their values.
enum : uint32_t {
    kSlot5Event = 0x6bbf8a5a, kSlot6Event = 0xcc313a42, kSlot7Event = 0x4c322bef, kSlot8Event = 0x6c322bf4,
    kSlot9Event = 0xac33830d, kSlot10Event = 0x6ea4b702, kSlot11Event = 0x6ea4b706,
    kFinishedEvent = 0x0bb6649b, kInterfaceEvent = 0x2bc250e7, kStreamEvent = 0x2bc78968, kStreamEvent2 = 0xcbc78960,
};
enum : uint32_t {
    kAKey = 0xcc313a33, kFlagKey = 0x8d696ffd, kIdKey = 0x8c322c22, kBKey = 0x8d80f069, kCKey = 0xad80f06f,
    kNameKey = 0x8c322c25, kName2Key = 0x8c322c27, kStreamNameKey = 0x2bcb73fc, kStreamWordKey = 0xabd5d0a2,
};
constexpr uint32_t kInterface7CIid = 0x206077d2;
constexpr uint32_t kMessage2StandardIid = 0x65297976;
// The states Init builds, in order.
constexpr uint32_t kStateIds[5] = {0xec313961, 0xec313964, 0xec313967, 0xec31396a, 0x0d8a50ad};
// The message types DoMessage handles.
enum : uint32_t { kMsgB3 = 0xb3, kMsgB4 = 0xb4, kMsgB6 = 0xb6, kMsg2bea = 0x2bea2063, kMsg4c32 = 0x4c3220e7, kMsg4c8e = 0x4c8e7905 };

using Self = cTSAudioMusicManager;

// ---- original callees nobody has replaced ----
// The workers (thiscall on the primary face).
void Worker2175cb(Self *s, uint32_t a, uint32_t flag) { OriginalFunction<void(__thiscall *)(Self *, uint32_t, uint32_t), 0x2175cb>()(s, a, flag); }
bool Worker217845(Self *s, uint32_t a, uint32_t b, int32_t c, const cIGZString *d, const cIGZString *e)
{
    return OriginalFunction<bool(__thiscall *)(Self *, uint32_t, uint32_t, int32_t, const cIGZString *, const cIGZString *), 0x217845>()(s, a, b, c, d, e);
}
bool Worker217be2(Self *s, uint32_t a) { return OriginalFunction<bool(__thiscall *)(Self *, uint32_t), 0x217be2>()(s, a); }
void Worker217c55(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x217c55>()(s); }
void Worker217490(Self *s, uint32_t a, uint32_t flag) { OriginalFunction<void(__thiscall *)(Self *, uint32_t, uint32_t), 0x217490>()(s, a, flag); }
void Worker21745e(Self *s, uint32_t a, const char *name) { OriginalFunction<void(__thiscall *)(Self *, uint32_t, const char *), 0x21745e>()(s, a, name); }
uint32_t NameToWord(Self *s, const char *name) { return OriginalFunction<uint32_t(__thiscall *)(Self *, const char *), 0x21737b>()(s, name); }
void WordToName(Self *s, uint32_t word, cRZString *out) { OriginalFunction<void(__thiscall *)(Self *, uint32_t, cRZString *), 0x2168f9>()(s, word, out); }
bool FindStationState(Self *s, uint32_t idLow, uint32_t idHigh, AudioRef *out)
{
    return OriginalFunction<bool(__thiscall *)(Self *, uint32_t, uint32_t, AudioRef *), 0x215c55>()(s, idLow, idHigh, out);
}
void AddState(Self *s, cIGZUnknown *state) { OriginalFunction<void(__thiscall *)(Self *, cIGZUnknown *), 0x2159c2>()(s, state); }
void Setup216d55(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x216d55>()(s); }
void Setup216a18(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x216a18>()(s); }
void Clear21753e(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x21753e>()(s); }
void Clear2174a7(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x2174a7>()(s); }
void Clear215a59(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x215a59>()(s); }
void Update21738b(Self *s) { OriginalFunction<void(__thiscall *)(Self *), 0x21738b>()(s); }
bool Message(uint32_t rva, Self *s, cIGZUnknown *message)
{
    using MessageFn = bool(__thiscall *)(Self *, cIGZUnknown *);
    switch (rva) {
    case 0x216270: return OriginalFunction<MessageFn, 0x216270>()(s, message);
    case 0x2162fd: return OriginalFunction<MessageFn, 0x2162fd>()(s, message);
    case 0x21670f: return OriginalFunction<MessageFn, 0x21670f>()(s, message);
    default: return OriginalFunction<MessageFn, 0x215ff8>()(s, message);
    }
}
void OnStream(bool second, Self *s, cITSAudioEvent *event, cRZString *name, uint32_t word)
{
    using StreamFn = void(__thiscall *)(Self *, cITSAudioEvent *, cRZString *, uint32_t);
    if (second) OriginalFunction<StreamFn, 0x2163d2>()(s, event, name, word);
    else OriginalFunction<StreamFn, 0x2166ba>()(s, event, name, word);
}
// The constructors of a music state (0x1c bytes) and of the custom playlist manager (0x18 bytes).
cIGZUnknown *NewState(uint32_t id)
{
    void *const memory = ::operator new(0x1c);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *, uint32_t), 0x239c97>()(memory, id) : nullptr;
}
cIGZUnknown *NewPlaylistManager()
{
    void *const memory = ::operator new(0x18);
    return memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0x23a84c>()(memory) : nullptr;
}
// A music state's members.
void *StateList(cIGZUnknown *state) { return OriginalFunction<void *(__thiscall *)(cIGZUnknown *), 0x237d60>()(state); }
uint64_t State237d27(cIGZUnknown *state, uint32_t a) { return OriginalFunction<uint64_t(__thiscall *)(cIGZUnknown *, uint32_t), 0x237d27>()(state, a); }
void State237bf4(cIGZUnknown *state, uint32_t a, uint32_t b, uint32_t c)
{
    OriginalFunction<void(__thiscall *)(cIGZUnknown *, uint32_t, uint32_t, uint32_t), 0x237bf4>()(state, a, b, c);
}
bool State237cfa(cIGZUnknown *state, uint32_t a, uint32_t b) { return OriginalFunction<bool(__thiscall *)(cIGZUnknown *, uint32_t, uint32_t), 0x237cfa>()(state, a, b); }
bool Object237cb9(cIGZUnknown *object, uint32_t b, uint32_t c, uint32_t a)
{
    return OriginalFunction<bool(__thiscall *)(cIGZUnknown *, uint32_t, uint32_t, uint32_t), 0x237cb9>()(object, b, c, a);
}
bool Static233d0a(uint32_t a, uint32_t b, uint32_t c) { return OriginalFunction<bool(__cdecl *)(uint32_t, uint32_t, uint32_t), 0x233d0a>()(a, b, c); }
// The player's members (+0x60).
void Player23a2f4(cIGZUnknown *player) { OriginalFunction<void(__thiscall *)(cIGZUnknown *), 0x23a2f4>()(player); }
void Player23a25e(cIGZUnknown *player, float value) { OriginalFunction<void(__thiscall *)(cIGZUnknown *, float), 0x23a25e>()(player, value); }
void Player23a457(cIGZUnknown *player, float value) { OriginalFunction<void(__thiscall *)(cIGZUnknown *, float), 0x23a457>()(player, value); }
void PlayerUpdate(cIGZUnknown *player, uint32_t now) { OriginalFunction<void(__thiscall *)(cIGZUnknown *, uint32_t), 0x23a6a7>()(player, now); }
bool PlayerFind(cIGZUnknown *player, uint32_t a, AudioRef *out)
{
    return OriginalFunction<bool(__thiscall *)(cIGZUnknown *, uint32_t, AudioRef *), 0x23a0db>()(player, a, out);
}
bool Playing238eef(cIGZUnknown *playing) { return OriginalFunction<bool(__thiscall *)(cIGZUnknown *), 0x238eef>()(playing); }
void Restart239555(cIGZUnknown *playing) { OriginalFunction<void(__thiscall *)(cIGZUnknown *), 0x239555>()(playing); }

cITSAudioEvent *NewEvent(uint32_t id)
{
    cITSAudioEvent *const e = new cTSAudioEvent(id);
    if (e) e->AddRef();
    return e;
}
void Drop(cITSAudioEvent *event)
{
    if (event) event->Release();
}

} // namespace

// ---- construction ----

// 0x214020: the service base (0x4bb90be0, priority -0x2dc6c0), the members in order (the never-written bytes stay), then
// the message ids.
cTSAudioMusicManager::cTSAudioMusicManager() : rz::cRZSystemService(kServiceId, kPriority)
{
    for (uint32_t id : {uint32_t(kMsg4c32), uint32_t(kMsgB6), uint32_t(kMsgB3), uint32_t(kMsg4c8e), uint32_t(kMsgB4),
                        uint32_t(kMsg2bea)})
        mMessageIds.insert(id);
}

// 0x21436e: the members in reverse order (the playlist manager first, the renderer last), then the service base.
cTSAudioMusicManager::~cTSAudioMusicManager() noexcept = default;

// 0x214c96.
cTSAudioMusicManager *__cdecl cTSAudioMusicManager::Create() { return new cTSAudioMusicManager; }

cTSAudioMusicManager *__fastcall cTSAudioMusicManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioMusicManager;
}

// ---- cIGZUnknown ----

// 0x21520e: 1 and cIGZSndEventHandler -> the +4 face, cITSAudioMusicManager -> this; counted through AddRef.
bool cTSAudioMusicManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1) *out = Handler();
    else if (riid == cITSAudioMusicManager::kIid) *out = static_cast<cITSAudioMusicManager *>(this);
    else if (riid == kHandlerIid) *out = Handler();
    else return false;
    AddRef();
    return true;
}

// 0x18fd70 (folded): the count plus one (not interlocked).
uint32_t cTSAudioMusicManager::AddRef() { return ++mRefs; }

// 0x215437: one less; at 0 deleted through the +0x14 face's slot 3 (the deleting destructor), 0.
uint32_t cTSAudioMusicManager::Release()
{
    const uint32_t refs = --mRefs;
    if (refs) return refs;
    delete this;
    return 0;
}

// ---- cITSAudioMusicManager / cIGZSystemService ----

bool cTSAudioMusicManager::OnRendererThread()
{
    uint32_t thread;
    nGZLog::StoreCurrentThreadId(&thread);
    return RawSlot<51>(mRenderer.Get()) == thread;
}

// 0x214f1f (+0x10 face's slot 8: the thunk 0x2151eb): nothing (true) with the "nomusic" switch or once initialised.
// Otherwise initialised: the renderer and the service 0x1af85bfb held (false without either), this registered for its
// messages, with the renderer (event handler, update target every 0x14) and the framework, the five states added, the
// playlist manager built, and the original's setup (0x216d55, 0x216a18) run.
bool cTSAudioMusicManager::Init()
{
    cIGZCmdLine *const commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    cRZString key;
    key.Construct("nomusic");
    const bool noMusic = commandLine->IsSwitchPresent(*key.AsInterface());
    key.Destruct();
    if (noMusic || mInitialized) return true;
    mInitialized = 1;

    bool ok = false;
    AudioRef renderer; // 0x20db8e
    GetService(renderer, kRendererId, kRendererIid);
    mRenderer.Assign(renderer.Get());
    if (mRenderer) {
        AudioRef service38; // 0x212e8c
        GetService(service38, kService38Id, kService38Iid);
        mService38.Assign(service38.Get());
        if (mService38) {
            reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(MessageTarget()), nullptr);
            RawSlot<15, void>(mRenderer.Get(), Handler());
            RawSlot<30, void>(mRenderer.Get(), UpdateTarget(), 0x14u);
            As<ifaces::cIGZFrameWork>(CachedFramework())->AddSystemService(As<ifaces::cIGZSystemService>(Service()));
            AudioRef state;
            for (uint32_t id : kStateIds) {
                state.Assign(NewState(id));
                AddState(this, state.Get());
            }
            Setup216d55(this);
            mPlaylistManager.Assign(NewPlaylistManager());
            Setup216a18(this);
            ok = true;
        }
    }
    return ok;
}

// 0x215576 (+0x10 face's slot 9: the thunk 0x215640): once initialised, unregistered (messages, framework, the renderer's
// update target and event handler), the original's clears (0x21753e, 0x2174a7, 0x215a59), the map +0xd0 and the stations
// emptied, and every held object dropped. True.
bool cTSAudioMusicManager::Shutdown()
{
    if (mInitialized) {
        mInitialized = 0;
        reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(MessageTarget()), nullptr);
        As<ifaces::cIGZFrameWork>(CachedFramework())->RemoveSystemService(As<ifaces::cIGZSystemService>(Service()));
        // ORIGINAL-BUG: the renderer is used unchecked (an Init that found no renderer leaves it null and initialised).
        RawSlot<31, void>(mRenderer.Get(), UpdateTarget());
        RawSlot<16, void>(mRenderer.Get(), Handler());
        Clear21753e(this);
        Clear2174a7(this);
        mMapD0.clear();
        Clear215a59(this);
        mStations.clear();
        mPlayer5C.Assign(nullptr);
        mPlayer60.Assign(nullptr);
        mService38.Assign(nullptr);
        mRenderer.Assign(nullptr);
        mPlaylistManager.Assign(nullptr);
    }
    return true;
}

// 0x215856: with a renderer and the player: on its thread the player's 0x23a2f4, else the event sent to the renderer.
void cTSAudioMusicManager::Slot5()
{
    if (!mRenderer || !mPlayer60) return;
    if (OnRendererThread()) return Player23a2f4(mPlayer60.Get());
    cITSAudioEvent *const e = NewEvent(kSlot5Event);
    RawSlot<8>(mRenderer.Get(), e);
    Drop(e);
}

// 0x215492: the event goes to the sound manager when `viaManager` (a bool), else to the sound probe's slot 8.
void cTSAudioMusicManager::Slot6(uint32_t a, uint32_t flag, uint32_t viaManager)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return Worker2175cb(this, a, flag);
    cITSAudioEvent *const e = NewEvent(kSlot6Event);
    e->SetUint32(kAKey, a);
    e->SetUint32(kFlagKey, static_cast<uint8_t>(flag));
    if (static_cast<uint8_t>(viaManager)) SoundManager()->PostEvent(e);
    else RawSlot<8>(SoundProbe(), e);
    Drop(e);
}

// 0x215648: on the renderer's thread the strings are copied for the worker.
void cTSAudioMusicManager::Slot7(uint32_t a, uint32_t b, int32_t c, const cIGZString *d, const cIGZString *e)
{
    if (!mRenderer) return;
    if (OnRendererThread()) {
        cRZString second, first;
        second.Construct(*e);
        first.Construct(*d);
        Worker217845(this, a, b, c, first.AsInterface(), second.AsInterface());
        first.Destruct();
        second.Destruct();
        return;
    }
    cITSAudioEvent *const ev = NewEvent(kSlot7Event);
    ev->SetUint32(kIdKey, a);
    ev->SetUint32(kBKey, b);
    ev->SetSint32(kCKey, c);
    ev->SetText(kNameKey, d);
    ev->SetText(kName2Key, e);
    SoundManager()->PostEvent(ev);
    Drop(ev);
}

// 0x21578a
void cTSAudioMusicManager::Slot8(uint32_t a)
{
    if (!mRenderer) return;
    if (OnRendererThread()) {
        Worker217be2(this, a);
        return;
    }
    cITSAudioEvent *const e = NewEvent(kSlot8Event);
    e->SetUint32(kIdKey, a);
    SoundManager()->PostEvent(e);
    Drop(e);
}

// 0x2158f8
void cTSAudioMusicManager::Slot9()
{
    if (!mRenderer) return;
    if (OnRendererThread()) return Worker217c55(this);
    cITSAudioEvent *const e = NewEvent(kSlot9Event);
    SoundManager()->PostEvent(e);
    Drop(e);
}

// 0x21536b: the event goes to the sound probe's slot 12.
void cTSAudioMusicManager::Slot10(uint32_t a, uint32_t flag)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return Worker217490(this, a, flag);
    cITSAudioEvent *const e = NewEvent(kSlot10Event);
    e->SetUint32(kAKey, a);
    e->SetUint32(kFlagKey, static_cast<uint8_t>(flag));
    RawSlot<12>(SoundProbe(), e);
    Drop(e);
}

// 0x215294: off the renderer's thread the name travels as its word (0x21737b; the handler turns it back, 0x2168f9).
void cTSAudioMusicManager::Slot11(uint32_t a, const char *name)
{
    if (!mRenderer) return;
    if (OnRendererThread()) return Worker21745e(this, a, name);
    cITSAudioEvent *const e = NewEvent(kSlot11Event);
    e->SetUint32(kIdKey, a);
    e->SetUint32(kNameKey, NameToWord(this, name));
    RawSlot<12>(SoundProbe(), e);
    Drop(e);
}

// 0x214d75: the id of the first station whose second name compares equal (case-sensitive) to `name`, else 0; each station
// is copied for the comparison.
uint64_t cTSAudioMusicManager::FindStationId(const cIGZString *name)
{
    for (const MusicStation &station : mStations) {
        const MusicStation copy(station);
        if (name->CompareTo(*copy.name2.s.AsInterface(), true) == 0) return uint64_t(copy.idHigh) << 32 | copy.idLow;
    }
    return 0;
}

// 0x214e5e: the station's state's list (0x237d60), else the list at +0x80.
void *cTSAudioMusicManager::Slot14(uint32_t idLow, uint32_t idHigh)
{
    AudioRef state;
    return FindStationState(this, idLow, idHigh, &state) ? StateList(state.Get()) : &mList80;
}

// 0x214ded: the station's state's 0x237d27, else 0.
uint64_t cTSAudioMusicManager::Slot15(uint32_t idLow, uint32_t idHigh, uint32_t a)
{
    AudioRef state;
    return FindStationState(this, idLow, idHigh, &state) ? State237d27(state.Get(), a) : 0;
}

// 0x214cd1: 0x233d0a, else any object of the list at +0x44 answering 0x237cb9 (each counted while asked).
bool cTSAudioMusicManager::Slot16(uint32_t a, uint32_t b, uint32_t c)
{
    if (Static233d0a(a, b, c)) return true;
    for (auto it = mList44.begin(); it != mList44.end();) {
        const AudioRef object(it->Get());
        if (Object237cb9(object.Get(), b, c, a)) return true;
        ++it;
    }
    return false;
}

// 0x214c3b: the station's state's 0x237bf4.
void cTSAudioMusicManager::Slot17(uint32_t idLow, uint32_t idHigh, uint32_t a, uint32_t b, uint32_t c)
{
    AudioRef state;
    if (FindStationState(this, idLow, idHigh, &state)) State237bf4(state.Get(), a, b, c);
}

// 0x214ebd: the station's state's 0x237cfa, else true.
bool cTSAudioMusicManager::Slot18(uint32_t idLow, uint32_t idHigh, uint32_t a, uint32_t b)
{
    AudioRef state;
    return FindStationState(this, idLow, idHigh, &state) ? State237cfa(state.Get(), a, b) : true;
}

// 0x2151f3
void cTSAudioMusicManager::Slot19(float value)
{
    if (mPlayer60) Player23a25e(mPlayer60.Get(), value);
}

// 0x215477
void cTSAudioMusicManager::Slot20(float value)
{
    if (mPlayer60) Player23a457(mPlayer60.Get(), value);
}

// ---- cIGZSndUpdateTarget (+8) ----

// 0x215998: the pending 0x21738b once, then the player's update.
void cTSAudioMusicManager::Update(uint32_t now)
{
    if (mUpdatePending) {
        mUpdatePending = 0;
        Update21738b(this);
    }
    if (mPlayer60) PlayerUpdate(mPlayer60.Get(), now);
}

// ---- cIGZMessageTarget2 (+0xc) ----

// 0x214b85: the message as cIGZMessage2Standard, by its type (read through the message given) to the original's handlers.
bool cTSAudioMusicManager::DoMessage(void *message)
{
    bool handled = false;
    AudioRef standard;
    cIGZUnknown *const m = static_cast<cIGZUnknown *>(message);
    if (m->QueryInterface(kMessage2StandardIid, standard.AsPPVoid())) {
        switch (RawSlot<4>(m)) {
        case kMsgB3: case kMsg4c8e: handled = Message(0x216270, this, standard.Get()); break;
        case kMsgB4: case kMsg2bea: handled = Message(0x21670f, this, standard.Get()); break;
        case kMsgB6: handled = Message(0x2162fd, this, standard.Get()); break;
        case kMsg4c32: handled = Message(0x215ff8, this, standard.Get()); break;
        default: break;
        }
    }
    return handled;
}

// ---- cIGZSndEventHandler (+4) ----

// 0x2145f8: by the event's id (slot 4 of the object given). The value cases query cITSAudioEvent first (false without it)
// and read the values last argument first, as the original.
bool cTSAudioMusicManager::HandleEvent(cIGZUnknown *event)
{
    using EventRef = cRZAutoRefCount<cITSAudioEvent>;
    const auto message = [event] { return RawSlot<6, cIGZMessage2Standard *>(event); };
    switch (RawSlot<4>(event)) {
    case kSlot5Event:
        if (mPlayer60) Player23a2f4(mPlayer60.Get());
        return true;
    case kSlot7Event: {
        EventRef e;
        if (!event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) return false;
        cRZString first, second;
        first.Construct();
        second.Construct();
        e->GetText(kNameKey, first.AsInterface());
        e->GetText(kName2Key, second.AsInterface());
        const int32_t c = e->GetSint32(kCKey);
        const uint32_t b = e->GetUint32(kBKey);
        const uint32_t a = e->GetUint32(kIdKey);
        const bool result = Worker217845(this, a, b, c, first.AsInterface(), second.AsInterface());
        second.Destruct();
        first.Destruct();
        return result;
    }
    case kInterfaceEvent: {
        // The message's object, its 0x206077d2 face kept at +0x7c (the old one dropped first).
        AudioRef object;
        object.Assign(message()->GetIGZUnknown());
        if (!object) return false;
        return object->QueryInterface(kInterface7CIid, mInterface7C.AsPPVoid());
    }
    case kFinishedEvent: {
        // The message's three words: what finished, and two that must be set. The player looks it up; while nothing is
        // waiting (+0xac empty, +0xa4 zero) it is restarted (logged) unless 0x238eef says so, else the next Update runs
        // 0x21738b.
        const uint32_t instance = message()->GetData1();
        const uint32_t data2 = message()->GetData2();
        const uint32_t data3 = message()->GetData3();
        if (!data2 || !data3) return false;
        AudioRef playing;
        if (PlayerFind(mPlayer60.Get(), instance, &playing)) {
            if (mStringAC.s.mString.size() == 0 && mWordA4 == 0) {
                if (!Playing238eef(playing.Get())) {
                    // 0x238ec0 (folded): the object embedded at +0x58 of what is playing; its slot 5 is the name.
                    cIGZUnknown *const source = reinterpret_cast<cIGZUnknown *>(reinterpret_cast<uint8_t *>(playing.Get()) + 0x58);
                    cRZStd log;
                    cRZStd *const object = log.Construct(8, Word(GameAddress<char>(kSourceFile)), 0x3fe);
                    const char *const name = RawSlot<5, const char *>(source);
                    VirtualSlot<void(__cdecl *)(cRZStd *, uint32_t, uint32_t, const char *, ...)>(object, 2)(
                        object, *GameAddress<uint32_t>(kMusicChannel), 5u, GameAddress<char>(kRestartFormat), instance, name);
                    Restart239555(playing.Get());
                }
            } else {
                mUpdatePending = 1;
            }
        }
        return false;
    }
    case kSlot8Event: {
        EventRef e;
        if (!event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) return false;
        return Worker217be2(this, e->GetUint32(kIdKey));
    }
    case kSlot10Event: {
        EventRef e;
        if (!event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) return false;
        const uint32_t flag = e->GetUint32(kFlagKey) != 0;
        Worker217490(this, e->GetUint32(kAKey), flag);
        return true;
    }
    case kSlot11Event: {
        EventRef e;
        if (!event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) return false;
        cRZString name;
        name.Construct();
        WordToName(this, e->GetUint32(kNameKey), &name);
        Worker21745e(this, e->GetUint32(kIdKey), name.ToChar());
        name.Destruct();
        return true;
    }
    case kSlot9Event: Worker217c55(this); return true;
    case kStreamEvent: case kStreamEvent2: {
        EventRef e;
        if (!event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) return false;
        cRZString name;
        name.Construct();
        e->GetText(kStreamNameKey, name.AsInterface());
        reinterpret_cast<cRZStringHelpers *>(&name)->LowerCase();
        if (name.StartsWith("stereo", 6, true)) {
            uint32_t word = 0;
            if (e->HasUint32(kStreamWordKey)) word = e->GetUint32(kStreamWordKey);
            OnStream(RawSlot<4>(event) == kStreamEvent2, this, e.Get(), &name, word);
        }
        name.Destruct();
        return true;
    }
    case kSlot6Event: {
        EventRef e;
        if (!event->QueryInterface(cITSAudioEvent::kIid, e.AsPPVoid())) return false;
        const uint32_t flag = e->GetUint32(kFlagKey) != 0;
        Worker2175cb(this, e->GetUint32(kAKey), flag);
        return true;
    }
    default: return false;
    }
}
