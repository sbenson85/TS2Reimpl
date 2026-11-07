// cTSMainToolbar (lane JE): the cITSMainToolbar face's slots (src/ui/cTSMainToolbar.h). Offsets in the comments are the
// object's (the original's bodies use the face's, 0x1c lower).
#include "ui/cTSMainToolbar.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h" // GetCachedServiceObjectD, UtilObjectB
#include "rz/RZFrameworkHelpers.h" // RZUnknownSlot
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "ui/cTSMainToolbarInternal.h"

#include <cstring>
#include <new>

using namespace toolbar;

namespace {

void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }

constexpr uint32_t kSpeedMessage = 0x4b46aebf;
constexpr uint32_t kPauseMessage = 0xcbccdece;
constexpr uint32_t kRefreshMessage = 0x6ca03a37;
constexpr uint32_t kControllerIid = 0x854450ee;
constexpr uint32_t kPanelNote = 0x492def1c;
constexpr uint32_t kPanelNoteOn = 0x4f5e3d96;

// The sound keys (game globals the audio code fills): pause, and play / fast / fastest by speed 2 / 3 / 4.
constexpr uint32_t kPauseSound = 0x10de554;
constexpr uint32_t kPlaySound = 0x10de558;
constexpr uint32_t kFastSound = 0x10de550;
constexpr uint32_t kFastestSound = 0x10de54c;

void PlaySound(uint32_t keyRva)
{
    if (SoundProbe()) RawSlot<11, void>(SoundProbe(), 1u, *GameAddress<uint32_t>(keyRva), 0u, 0u, 0u);
}
// The speed's button sound (2 play, 3 fast, 4 fastest; others none).
void PlaySpeedSound(int32_t speed)
{
    switch (speed) {
    case 2: PlaySound(kPlaySound); break;
    case 3: PlaySound(kFastSound); break;
    case 4: PlaySound(kFastestSound); break;
    default: break;
    }
}

// A message with one value posted through the message server's slot 3, then released.
void PostValue(uint32_t id, uint32_t value)
{
    cRZMessage2Standard *const message = NewMessage(id);
    As<ifaces::cIGZMessage2Standard>(message)->SetData1(value);
    As<ifaces::cIGZMessageServer>(MessageServer())->MessageSend(message);
    ReleaseOf(message);
}

// The panel object's members (game code, not replaced): select with a note and an object, clear, and two routes the panel
// modes 3 / 4 take.
void PanelSelect(void *panel, uint32_t note, void *object)
{
    OriginalFunction<void(__thiscall *)(void *, uint32_t, void *), 0x61b843>()(panel, note, object);
}
void PanelClear(void *panel) { OriginalFunction<void(__thiscall *)(void *), 0x620643>()(panel); }
void PanelMode4(void *panel, uint32_t a, uint32_t b)
{
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x61b61e>()(panel, a, b);
}
void PanelMode3(void *panel, uint32_t a, uint32_t b)
{
    OriginalFunction<void(__thiscall *)(void *, uint32_t, uint32_t), 0x61b75f>()(panel, a, b);
}

UtilObjectB *NewNote(uint32_t id)
{
    auto *const memory = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    return memory ? memory->Construct(id) : nullptr;
}

} // namespace

// 4 0x61fed0
void cTSMainToolbar::MarkModified() { mModified = true; }

// 5 0x61b94c: any of the three flags, or the pause byte or the two words differing from the snapshot.
bool cTSMainToolbar::IsModified()
{
    return mModified || mFlag32 || mFlag33 || *reinterpret_cast<const uint8_t *>(&mSavedPaused) !=
                                                    *reinterpret_cast<const uint8_t *>(&mPaused) ||
           mSavedWord50 != mWord50 || mSavedWord54 != mWord54;
}

bool cTSMainToolbar::Flag32() { return mFlag32; }

// 7 0x6208e5 / 8 0x61fddd: the persist system's slots 72 / 73 with the serializable face; true when they answer 0.
bool cTSMainToolbar::SaveTo(uint32_t stream)
{
    return As<ifaces::cITSPersistSystem>(PersistSystem())->WriteResourceToFileRef(As<ifaces::cIGZSerializable>(static_cast<cIGZSerializable *>(this)), reinterpret_cast<ifaces::cITSObjectFileRef *>(stream), 2u) == 0;
}
bool cTSMainToolbar::LoadFrom(uint32_t stream)
{
    return As<ifaces::cITSPersistSystem>(PersistSystem())->ReadResourceFromSegment(static_cast<cIGZSerializable *>(this), reinterpret_cast<void *>(stream), 2u, 0u) == 0;
}

// 9 0x61e6db
void cTSMainToolbar::ResetGameState()
{
    mPaused = false;
    mWord50 = 0;
    mWord54 = 1;
    mWord64 = 0;
}

void cTSMainToolbar::SetWindow78(void *window) { mWindow78 = window; }
void *cTSMainToolbar::Window78() { return mWindow78; }

// 12 0x621b33: null releases the controller; otherwise the held one is dropped (the slot cleared first) and the new one
// asked for 0x854450ee into the slot, nothing more when it refuses. Then slot 77 with the argument and two trace calls.
void cTSMainToolbar::SetController(cIGZUnknown *controller)
{
    if (!controller) {
        reinterpret_cast<RZUnknownSlot *>(&mController)->Assign(nullptr);
    } else {
        void **const slot = reinterpret_cast<void **>(&mController);
        if (void *const old = *slot) {
            *slot = nullptr;
            RawSlot<2>(old);
        }
        if (!controller->QueryInterface(kControllerIid, slot)) return;
    }
    Window90Key(Word(controller));
    TraceA(0, 0);
    const uint32_t value = 0xdb;
    Trace(0x620b15, &value);
}

// 13 0x61e388 / 80 0x622554: the controller's slot 15 (80 without the null test).
uint32_t cTSMainToolbar::ControllerSlot15()
{
    void *const controller = *reinterpret_cast<void *const *>(&mController);
    return controller ? RawSlot<15>(controller) : 0;
}
uint32_t cTSMainToolbar::ControllerSlot15Unchecked() { return RawSlot<15>(*reinterpret_cast<void *const *>(&mController)); }

// 14 0x61e32f: the UCP state made on first use (operator new 0x14, its constructor 0x671b29, the +8 face kept).
void *cTSMainToolbar::UCPState()
{
    void **const slot = reinterpret_cast<void **>(&mUCPState);
    if (!*slot) {
        void *const memory = ::operator new(0x14);
        void *state = nullptr;
        if (memory) state = OriginalFunction<void *(__thiscall *)(void *), 0x671b29>()(memory);
        reinterpret_cast<RZUnknownSlot *>(slot)->Assign(state ? static_cast<uint8_t *>(state) + 8 : nullptr);
    }
    return *slot;
}

// 15 0x61e248: the +0x78 window as 0x5386d516, its slot 9 for 0x17d5d3d7, that one's slot 6 with the key; 0 on a refusal.
uint32_t cTSMainToolbar::Window78Value(uint32_t key)
{
    if (!mWindow78) return 0;
    void *first = nullptr;
    uint32_t result = 0;
    if (RawSlot<0, bool>(mWindow78, 0x5386d516u, &first)) {
        void *second = nullptr;
        if (RawSlot<9, bool>(first, 0x17d5d3d7u, &second)) {
            result = RawSlot<6>(second, key);
            ReleaseOf(second);
            ReleaseOf(first);
            return result;
        }
        ReleaseOf(second);
    }
    ReleaseOf(first);
    return 0;
}

// 16 0x62190a: with the clock: a new speed is posted (message 0x4b46aebf), unpausing first when asked and paused, and
// its button sound played when asked and the clock is not paused; the same speed only unpauses (with the sound flag).
void cTSMainToolbar::SetGameSpeed(int32_t speed, bool fromUser, bool sound)
{
    void *const clock = Clock();
    if (!clock) return;
    if (speed == RawSlot<21, int32_t>(clock)) {
        if (fromUser && RawSlot<26, bool>(clock)) SetPaused(false, sound);
        return;
    }
    if (fromUser && RawSlot<26, bool>(clock)) SetPaused(false, false);
    PostValue(kSpeedMessage, static_cast<uint32_t>(speed));
    if (sound && !RawSlot<26, bool>(clock)) PlaySpeedSound(speed);
}

// 17 0x6217a6: the pause byte set and posted (message 0xcbccdece) unless unpausing while pauses are pushed outside panel
// mode 0xb; a changed state's sound when asked (pause, or the clock's speed).
void cTSMainToolbar::SetPaused(bool paused, bool sound)
{
    const uint8_t raw = *reinterpret_cast<const uint8_t *>(&paused);
    const uint8_t was = *reinterpret_cast<const uint8_t *>(&mPaused);
    *reinterpret_cast<uint8_t *>(&mPaused) = raw;
    if (!raw && PausePushed() && PanelMode() != 0xb) return;
    cRZMessage2Standard *const message = NewMessage(kPauseMessage);
    As<ifaces::cIGZMessage2Standard>(message)->SetData1(static_cast<uint32_t>(raw));
    As<ifaces::cIGZMessageServer>(MessageServer())->MessageSend(message);
    if (sound && was != raw) {
        if (*reinterpret_cast<const uint8_t *>(&mPaused)) {
            PlaySound(kPauseSound);
        } else if (void *const clock = Clock()) {
            PlaySpeedSound(RawSlot<21, int32_t>(clock));
        }
    }
    ReleaseOf(message);
}

// 18 0x61fe0f / 19 0x622462: the pause counter; the first push and the last pop post pause 1 / 0 unless paused.
void cTSMainToolbar::PushPause()
{
    if (mPauseCount++ == 0 && !*reinterpret_cast<const uint8_t *>(&mPaused)) PostValue(kPauseMessage, 1);
}
void cTSMainToolbar::PopPause()
{
    if (--mPauseCount == 0 && !*reinterpret_cast<const uint8_t *>(&mPaused)) PostValue(kPauseMessage, 0);
}

// 20 0x61fc1e: an unsigned compare (seta).
bool cTSMainToolbar::PausePushed() { return static_cast<uint32_t>(mPauseCount) > 0; }

// 21 0x620bd9: outside +0xaa, the toggle flips when it equals the argument; the argument pushes (true) or pops a pause.
void cTSMainToolbar::TogglePauseBy(bool paused)
{
    const uint8_t raw = *reinterpret_cast<const uint8_t *>(&paused);
    if (mFlagAA || *reinterpret_cast<const uint8_t *>(&mPauseToggle) != raw) return;
    *reinterpret_cast<uint8_t *>(&mPauseToggle) = raw ^ 1;
    if (raw)
        PopPause();
    else
        PushPause();
}

void cTSMainToolbar::Nothing22(uint32_t) {}
int32_t cTSMainToolbar::PanelMode() { return mPanelMode; }
uint32_t cTSMainToolbar::Word5C() { return mWord5C; }
int32_t cTSMainToolbar::Word50() { return mWord50; }
int32_t cTSMainToolbar::Word54() { return mWord54; }
void cTSMainToolbar::SetWordB4(uint16_t value) { mWordB4 = value; }
uint16_t cTSMainToolbar::WordB4() { return mWordB4; }

// 32 0x61dfc9: with the +0x78 window, the callback 0x61e12f (__cdecl) with it and the value's address.
void cTSMainToolbar::NotifyWindow78(uint8_t value)
{
    if (mWindow78)
        OriginalFunction<uint32_t(__cdecl *)(uint32_t, uint32_t, void *, uint8_t *), 0x61e12f>()(0, 0, mWindow78, &value);
}

void cTSMainToolbar::SetFlag9F(bool value) { mFlag9F = value; }
bool cTSMainToolbar::Flag9F() { return mFlag9F; }
bool cTSMainToolbar::FlagA0() { return mFlagA0; }
void *cTSMainToolbar::Window90() { return *reinterpret_cast<void *const *>(&mWindow90); }

// 39 0x61b33c: the app's slot 26 object's selection (its slot 39) handed to slot 38.
void cTSMainToolbar::FocusSelected()
{
    void *const app = GetCachedSingleton();
    if (!app) return;
    void *const view = RawSlot<26, void *>(app);
    if (!view) return;
    Slot38(RawSlot<39>(view));
}

// 40 0x61b09a: FocusSelected when asked (the flag's low byte), then the state holder's slot 0 with the flag's whole word.
void cTSMainToolbar::Slot40(uint32_t flag)
{
    if (static_cast<uint8_t>(flag)) FocusSelected();
    void *const holder = StateHolder();
    if (holder) RawSlot<0, void>(holder, flag);
    ReleaseVirtual(holder);
}

// 41 0x61b120: with the flag (low byte) and an object: slot 38 with it, and +0xa4 becomes its slot 8's slot 61's slot 28
// (else 0); then the state holder's slot 0 with the flag's whole word.
void cTSMainToolbar::Slot41(void *object, uint32_t flag)
{
    uint32_t word = 0;
    if (static_cast<uint8_t>(flag) && object) {
        Slot38(Word(object));
        word = RawSlot<28>(RawSlot<61, void *>(RawSlot<8, void *>(object)));
    }
    mWordA4 = word;
    void *const holder = StateHolder();
    if (holder) RawSlot<0, void>(holder, flag);
    ReleaseVirtual(holder);
}

// 42 0x620997: the app's slot 26 object's slot 41.
void cTSMainToolbar::SelectedSlot41()
{
    void *const app = GetCachedSingleton();
    if (app && RawSlot<26, void *>(app)) RawSlot<41, void>(RawSlot<26, void *>(app));
}

// 43 0x61e1bd: +0xa4 cleared unless the state holder exists and its slot 16 says so; +0xa4 returned.
uint32_t cTSMainToolbar::WordA4()
{
    void *const holder = StateHolder();
    if (!holder || !RawSlot<16, bool>(holder)) mWordA4 = 0;
    const uint32_t word = mWordA4;
    ReleaseVirtual(holder);
    return word;
}

// 44 0x622301: the +0xdc window shown (its slot 5 with the argument) when asked and +0x124 is set, else hidden (slot 6).
void cTSMainToolbar::ShowWindowDC(uint32_t show)
{
    void *const window = *reinterpret_cast<void *const *>(&mWindowDC);
    if (!window) return;
    if (show && mFlag124)
        RawSlot<5, void>(window, show);
    else
        RawSlot<6, void>(window);
}

cRZString *cTSMainToolbar::HelpText() { return &mHelpText.s; }
void *cTSMainToolbar::Window98() { return *reinterpret_cast<void *const *>(&mWindow98); }
void *cTSMainToolbar::Window94() { return *reinterpret_cast<void *const *>(&mWindow94); }

// 48 0x61dfe6: in panel mode 0xb with a panel: selected with 0x4f5e3d96 and no object when on, cleared when off.
void cTSMainToolbar::PanelSelect48(bool on)
{
    if (mPanelMode != 0xb || !mPanel) return;
    if (on)
        PanelSelect(mPanel, kPanelNoteOn, nullptr);
    else
        PanelClear(mPanel);
}

// 49 0x6209c2: panel mode 0xb (asked for through slot 25 when not in it): a note object for the id handed to the panel's
// select (0x4f5e3d96 when on, else 0x492def1c).
void cTSMainToolbar::PanelSelect49(uint32_t id, bool on)
{
    if (mPanelMode != 0xb) {
        Slot25(0xb, 1);
        if (mPanelMode != 0xb) return;
    }
    UtilObjectB *const note = NewNote(id);
    if (note) RawSlot<1>(note);
    if (note) PanelSelect(mPanel, *reinterpret_cast<const uint8_t *>(&on) ? kPanelNoteOn : kPanelNote, note);
    ReleaseOf(note);
}

// 50 0x620915 / 61 0x620926: the panel's routes in modes 4 / 3.
void cTSMainToolbar::PanelSlot50(uint32_t a, uint32_t b)
{
    if (mPanelMode == 4) PanelMode4(mPanel, a, b);
}
void cTSMainToolbar::PanelSlot61(uint32_t a, uint32_t b)
{
    if (mPanelMode == 3) PanelMode3(mPanel, a, b);
}

// 51 0x621a6b: +0x9d flips when it equals the argument; then the state service's slot 38 object's slot 13.
void cTSMainToolbar::ToggleFlag9D(bool value)
{
    const uint8_t raw = *reinterpret_cast<const uint8_t *>(&value);
    if (*reinterpret_cast<const uint8_t *>(&mFlag9D) != raw) return;
    *reinterpret_cast<uint8_t *>(&mFlag9D) = raw ^ 1;
    if (void *const object = RawSlot<38, void *>(StateService())) RawSlot<13, void>(object);
}

void cTSMainToolbar::SetFlagAC(bool value) { mFlagAC = value; }
void cTSMainToolbar::SetFlagAD(bool value) { mFlagAD = value; }
bool cTSMainToolbar::Flag9C() { return mFlag9C; }
bool cTSMainToolbar::Flag9DClear() { return *reinterpret_cast<const uint8_t *>(&mFlag9D) == 0; }
bool cTSMainToolbar::Flag9E() { return mFlag9E; }
bool cTSMainToolbar::FlagAC() { return mFlagAC; }
bool cTSMainToolbar::FlagAD() { return mFlagAD; }
void cTSMainToolbar::SetValueB0(float value) { mValueB0 = value; }

// 60 0x61e4d0: false when the value is within 32 representable steps of zero (its bits as an ordered integer); else
// stored to `out` (when given), true.
bool cTSMainToolbar::ValueB0(float *out)
{
    const float value = mValueB0;
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const uint32_t sign = bits >> 31;
    const uint32_t ordered = (((0u - sign) >> 1) ^ bits) + sign;
    if (((static_cast<uint32_t>(static_cast<int32_t>(ordered) >> 26) ^ ordered) & 0xffffffe0u) == 0) return false;
    if (out) *out = value;
    return true;
}

// 62 0x6201dd: a note object for the id handed to the panel's mode-3 / mode-4 route (not counted here).
void cTSMainToolbar::PanelSelect62(uint32_t id)
{
    if (mPanelMode == 3)
        PanelMode3(mPanel, kPanelNote, Word(NewNote(id)));
    else if (mPanelMode == 4)
        PanelMode4(mPanel, kPanelNote, Word(NewNote(id)));
}

// 63 0x621cf5
void cTSMainToolbar::SetWord64(int32_t value)
{
    mWord64 = value;
    if (value == 2) mWord68 = 2;
}
int32_t cTSMainToolbar::Word64() { return mWord64; }
int32_t cTSMainToolbar::Word68() { return mWord68; }
int32_t cTSMainToolbar::Zoom() { return mZoom; }
void cTSMainToolbar::SetZoom(int32_t value) { mZoom = value; }

// 68 0x61b59f: the zoom moved by `delta` when it stays within 1..7.
void cTSMainToolbar::StepZoom(int32_t delta)
{
    const int32_t zoom = mZoom + delta;
    if (static_cast<uint32_t>(zoom - 1) <= 6) mZoom = zoom;
}

// 69 0x61b910: the clock's slot 49 with 2 when its slot 48 is 0 or 3, with 0 when 1 or 2.
void cTSMainToolbar::ClockSlot69()
{
    void *const clock = Clock();
    if (!clock) return;
    switch (RawSlot<48>(clock)) {
    case 0:
    case 3: RawSlot<49, void>(clock, 2u); break;
    case 1:
    case 2: RawSlot<49, void>(clock, 0u); break;
    default: break;
    }
}

// 70 0x621aa9 / 72 0x620c03: the mode set on or back (live, or the other mode when it was on); the state service's slot
// 53 object named the mode (slot 20), and the first switch on clears its slot 25 (-1); build also runs FocusSelected.
void cTSMainToolbar::SetBuildMode(bool on)
{
    int32_t mode = mMode != 1 ? mMode : 0;
    if (void *const view = RawSlot<53, void *>(StateService())) {
        RawSlot<20, void>(view, on ? "Build" : (mode ? "Ceiling" : "Live"));
        bool *const once = GameAddress<bool>(0x10dc598);
        if (on && !*once) {
            *once = true;
            RawSlot<25, void>(view, 0xffffffffu);
            FocusSelected();
        }
    }
    if (on) mode = 1;
    mMode = mode;
}
bool cTSMainToolbar::IsBuildMode() { return mMode == 1; }
bool __fastcall cTSMainToolbarAdapters::IsBuildMode(cITSMainToolbar *face, void *)
{
    return static_cast<cTSMainToolbar *>(face)->cTSMainToolbar::IsBuildMode();
}

void cTSMainToolbar::SetCeilingMode(bool on)
{
    int32_t mode = mMode != 2 ? mMode : 0;
    if (void *const view = RawSlot<53, void *>(StateService())) {
        RawSlot<20, void>(view, on ? "Ceiling" : (mode ? "Build" : "Live"));
        bool *const once = GameAddress<bool>(0x10dc599);
        if (on && !*once) {
            *once = true;
            RawSlot<25, void>(view, 0xffffffffu);
        }
    }
    if (on) mode = 2;
    mMode = mode;
}
bool cTSMainToolbar::IsCeilingMode() { return mMode == 2; }
bool cTSMainToolbar::Flag129Clear() { return *reinterpret_cast<const uint8_t *>(&mFlag129) == 0; }

// 75 0x620937: the +0x90 window (slot 36) set up: four slot-8 calls, slot 6 with the two arguments, slot 7.
void cTSMainToolbar::Window90Setup(uint32_t a, uint32_t b)
{
    void *const window = Window90();
    if (!window) return;
    RawSlot<8, void>(window, 0u, 0u);
    RawSlot<8, void>(window, 0x1000000u, 0u);
    RawSlot<8, void>(window, 1u, 0u);
    RawSlot<8, void>(window, 0x1000001u, 0u);
    RawSlot<6, void>(window, 0u, a, b, 0u);
    RawSlot<7, void>(window, 0u, 1u, 0u);
}

// 77 0x620448: the +0x90 window's slot 3 with the key; slot 78 when it answers true.
void cTSMainToolbar::Window90Key(uint32_t key)
{
    if (RawSlot<3, bool>(Window90(), key)) Window90Bind();
}

// 78 0x61abfd / 79 0x6204bf: the +0x90 window's key bindings made (slots 6 and 7) / dropped (slot 8).
void cTSMainToolbar::Window90Bind()
{
    void *const window = Window90();
    if (!window) return;
    RawSlot<6, void>(window, 0x3000001u, 0x692ee271u, 0u, 0u);
    RawSlot<6, void>(window, 0x80000002u, 0x692ee7eeu, 0u, 0u);
    RawSlot<7, void>(window, 0x80000002u, 0x82000002u, 0u);
    RawSlot<7, void>(window, 0x80000002u, 0x83000002u, 0u);
    RawSlot<6, void>(window, 0x80000004u, 0x4bb0ef8eu, 0u, 0u);
    RawSlot<7, void>(window, 0x80000004u, 0x82000004u, 0u);
    RawSlot<7, void>(window, 0x80000004u, 0x81000004u, 0u);
    RawSlot<7, void>(window, 0x80000004u, 0x83000004u, 0u);
    RawSlot<7, void>(window, 0x80000004u, 0x81000002u, 0u);
    RawSlot<6, void>(window, 0x6000000u, 0xc94fe367u, 0u, 0u);
}
void cTSMainToolbar::Window90Unbind()
{
    void *const window = Window90();
    if (!window) return;
    RawSlot<8, void>(window, 0x3000001u, 0x692ee271u);
    RawSlot<8, void>(window, 0x80000002u, 0x692ee7eeu);
    RawSlot<8, void>(window, 0x82000002u, 0x692ee7eeu);
    RawSlot<8, void>(window, 0x83000002u, 0x692ee7eeu);
    RawSlot<8, void>(window, 0x80000004u, 0x4bb0ef8eu);
    RawSlot<8, void>(window, 0x82000004u, 0x4bb0ef8eu);
    RawSlot<8, void>(window, 0x81000004u, 0x4bb0ef8eu);
    RawSlot<8, void>(window, 0x83000004u, 0x4bb0ef8eu);
    RawSlot<8, void>(window, 0x81000002u, 0x4bb0ef8eu);
    RawSlot<8, void>(window, 0x6000000u, 0xc94fe367u);
}

// 81 0x61df34: -1 while busy (+0xab). Through the service object D (its slot 42, between a pause push and pop) when
// asked, its result returned; otherwise its slot 3 object's slot 19 with the object and the object's slot 83
// (0x1000, 1) when there is one, and `fallback` returned.
uint32_t cTSMainToolbar::RunPaused(void *object, bool viaService, uint32_t fallback)
{
    if (mBusy) return 0xffffffffu;
    mBusy = true;
    uint32_t result = fallback;
    if (viaService) {
        PushPause();
        result = RawSlot<42>(GetCachedServiceObjectD(), object);
        PopPause();
    } else if (GetCachedServiceObjectD() && RawSlot<3, void *>(GetCachedServiceObjectD())) {
        RawSlot<19, void>(RawSlot<3, void *>(GetCachedServiceObjectD()), object);
        RawSlot<83, void>(object, 0x1000u, 1u);
    }
    mBusy = false;
    return result;
}

void *cTSMainToolbar::WindowB8() { return *reinterpret_cast<void *const *>(&mWindowB8); }

// 84 0x61e1aa: the +0xbc window's part at +0xf8 (null without a window).
void *cTSMainToolbar::WindowBCPart()
{
    auto *const window = *reinterpret_cast<uint8_t *const *>(&mWindowBC);
    return window ? window + 0xf8 : nullptr;
}

void *cTSMainToolbar::WindowD8() { return *reinterpret_cast<void *const *>(&mWindowD8); }
RZUnknownSlot *cTSMainToolbar::SetWindowD8(void *window) { return reinterpret_cast<RZUnknownSlot *>(&mWindowD8)->Assign(window); }
bool cTSMainToolbar::Flag125() { return mFlag125; }
void *cTSMainToolbar::WindowE0() { return *reinterpret_cast<void *const *>(&mWindowE0); }
RZUnknownSlot *cTSMainToolbar::SetWindowE0(void *window) { return reinterpret_cast<RZUnknownSlot *>(&mWindowE0)->Assign(window); }
bool cTSMainToolbar::Flag127() { return mFlag127; }
bool cTSMainToolbar::FlagAB() { return mBusy; }
bool cTSMainToolbar::Flag128() { return mFlag128; }

// 93 0x6200be: message 0x6ca03a37 posted (one stack dword, never read: ret 4).
void cTSMainToolbar::PostRefresh(uint32_t)
{
    cRZMessage2Standard *const message = NewMessage(kRefreshMessage);
    As<ifaces::cIGZMessageServer>(MessageServer())->MessageSend(message);
    ReleaseOf(message);
}

// ---- slots still game code (forwarded on the toolbar face) ----

uint32_t cTSMainToolbar::Slot25(uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 0x621006>()(
        static_cast<cITSMainToolbar *>(this), a, b);
}
uint32_t cTSMainToolbar::Slot27(uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x620c7e>()(static_cast<cITSMainToolbar *>(this), a);
}
uint32_t cTSMainToolbar::Slot29(uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x621ba2>()(static_cast<cITSMainToolbar *>(this), a);
}
uint32_t cTSMainToolbar::Slot37(uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x61b1d5>()(static_cast<cITSMainToolbar *>(this), a);
}
uint32_t cTSMainToolbar::Slot38(uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x61b36c>()(static_cast<cITSMainToolbar *>(this), a);
}
uint32_t cTSMainToolbar::Slot76(uint32_t a, uint32_t b, uint32_t c)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0x61fc26>()(
        static_cast<cITSMainToolbar *>(this), a, b, c);
}
uint32_t cTSMainToolbar::Slot82(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0x61fed5>()(
        static_cast<cITSMainToolbar *>(this), a, b, c, d, e);
}
